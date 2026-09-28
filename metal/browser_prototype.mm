// Small local browser UI prototype: one Metal render and an HTTP RGBA preview.

#include "persistent_renderer.h"
#include "browser_settings.h"

#include "buddha.h"
#include "image_pipeline.h"
#include "settings.h"

#import <AppKit/AppKit.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using buddha_browser::parse_display;
using buddha_browser::parse_settings;
using buddha_browser::request_settings;
using buddha_image::adjustments;

constexpr uint32_t batch_samples = 1U << 27;
using command_buffer = id<MTLCommandBuffer>; // render_session::id hides Objective-C's id
volatile sig_atomic_t shutting_down = 0;

double seconds_since(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
}

settings session_settings(const request_settings &c) {
    if (!std::filesystem::is_regular_file(BUDDHA_EXCLUSION_MAP))
        throw std::runtime_error("default exclusion map is missing: " BUDDHA_EXCLUSION_MAP);
    return buddha_browser::make_settings(c);
}

// Progress and timing counters, copied from the session to the status response in one piece.
struct session_stats {
    uint64_t samples = 0;
    double elapsed = 0;
    double batch_seconds = 0;   // GPU execution time of completed batches
    double capture_seconds = 0; // histogram copies; the GPU waits for these
    uint64_t capture_count = 0;
    double preview_seconds = 0; // frames from a new capture, built while the next batch runs
    uint64_t preview_count = 0;
    double recolor_seconds = 0; // display-only frames from the cached float image
    uint64_t recolor_count = 0;
};

struct render_session {
    uint64_t id;
    std::unique_ptr<buddha> image;
    std::unique_ptr<buddha_metal::persistent_renderer> gpu;
    command_buffer in_flight = nil;
    session_stats stats;
    std::chrono::steady_clock::time_point start, last_capture;
    double elapsed_before_pause = 0;
    bool paused = false;

    buddha_image::tone_mapper tone;
    std::vector<uint32_t> snapshot;
    std::optional<uint64_t> snapshot_samples; // stats.samples when snapshot was captured
    std::optional<uint64_t> base_samples;     // snapshot_samples when base was tone-mapped
    buddha_image::float_image base, adjusted;
    buddha_image::adjustment_buffers buffers;

    render_session(uint64_t render_id, const request_settings &c)
        : id(render_id), image(std::make_unique<buddha>(session_settings(c))), tone(image->s) {
        buddha_metal::check_histogram_layout<buddha::vector_type>();
        gpu = std::make_unique<buddha_metal::persistent_renderer>(
            buddha_metal::default_device(),
            buddha_metal::make_parameters(image->s, image->core.size), image->core.data.data(),
            image->core.data.size(), image->raw.data(), buddha_metal::histogram_bytes(image->raw));
        start = last_capture = std::chrono::steady_clock::now();
    }

    // The GPU writes image->raw directly; never free it under a running batch.
    ~render_session() {
        if (in_flight)
            [in_flight waitUntilCompleted];
    }

    double elapsed() const {
        return elapsed_before_pause + (paused ? 0 : seconds_since(start));
    }

    session_stats current_stats() const {
        session_stats current = stats;
        current.elapsed = elapsed();
        return current;
    }

    void pause() {
        if (!paused) {
            elapsed_before_pause = elapsed();
            paused = true;
        }
    }

    void resume() {
        if (paused) {
            start = std::chrono::steady_clock::now();
            paused = false;
        }
    }

    void dispatch(std::random_device &random) {
        in_flight = gpu->dispatch(0, batch_samples, random(), random());
    }

    // Waits for the batch in flight. Nothing writes the histogram again until the next dispatch.
    void finish() {
        if (!in_flight)
            return;
        command_buffer command = in_flight;
        in_flight = nil;
        buddha_metal::persistent_renderer::wait(command);
        stats.samples += batch_samples;
        stats.batch_seconds += command.GPUEndTime - command.GPUStartTime;
    }

    // Copies the histogram between batches. The copy, not the conversion, is the only preview
    // work the GPU waits for.
    void capture() {
        if (in_flight)
            throw std::logic_error("histogram capture while a Metal batch may write it");
        if (snapshot_samples == stats.samples)
            return;
        const auto begin = std::chrono::steady_clock::now();
        buddha_image::capture(image->raw, snapshot);
        snapshot_samples = stats.samples;
        stats.capture_seconds += seconds_since(begin);
        ++stats.capture_count;
        last_capture = begin;
    }

    bool has_unconverted_capture() const { return snapshot_samples != base_samples; }

    // Builds a frame from the latest capture. It reads only the snapshot, so it may run while a
    // batch writes the histogram.
    std::shared_ptr<const std::vector<uint8_t>> frame(const adjustments &display) {
        const auto begin = std::chrono::steady_clock::now();
        const bool new_histogram = has_unconverted_capture();
        if (new_histogram) {
            tone.map(snapshot, image->s, base);
            base_samples = snapshot_samples;
        }
        buddha_image::adjust(base, display, adjusted, buffers);
        auto rgba = std::make_shared<std::vector<uint8_t>>();
        buddha_image::to_rgba8(adjusted, *rgba);
        const double seconds = seconds_since(begin);
        if (new_histogram) {
            stats.preview_seconds += seconds;
            ++stats.preview_count;
        } else {
            stats.recolor_seconds += seconds;
            ++stats.recolor_count;
        }
        return rgba;
    }
};

struct state {
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<request_settings> pending;
    bool stop = false;
    bool pause = false;
    bool resume = false;
    bool shutdown = false;
    uint64_t render_id = 0;
    uint64_t frame_render_id = 0;
    uint64_t frame_revision = 0;
    uint64_t frame_samples = 0;
    uint64_t display_revision = 0;
    uint64_t frame_display_revision = 0;
    adjustments display;
    session_stats stats;
    double cre = 0, cim = 0, scale = 0;
    uint32_t output_width = 0, output_height = 0;
    std::string phase = "idle";
    std::string error;
    std::shared_ptr<const std::vector<uint8_t>> frame;
};

// Call with the mutex held: the published frame lacks the session's latest completed batch or the
// current display settings.
bool needs_frame(const state &shared, const render_session &session) {
    return shared.render_id == session.id && !shared.pending &&
           (shared.frame_render_id != session.id || shared.frame_samples != session.stats.samples ||
            shared.frame_display_revision != shared.display_revision);
}

void publish_frame(state &shared, render_session &session) {
    adjustments display;
    uint64_t display_revision = 0;
    {
        std::lock_guard lock(shared.mutex);
        if (shared.render_id != session.id || shared.pending)
            return;
        display = shared.display;
        display_revision = shared.display_revision;
    }
    auto frame = session.frame(display);
    std::lock_guard lock(shared.mutex);
    if (shared.render_id == session.id && !shared.pending &&
        shared.display_revision == display_revision) {
        shared.frame = std::move(frame);
        shared.frame_render_id = session.id;
        shared.frame_samples = *session.base_samples;
        shared.frame_display_revision = display_revision;
        ++shared.frame_revision;
        shared.stats = session.current_stats();
    }
}

void render_worker(state &shared) {
    std::optional<render_session> session;
    std::random_device random;
    bool running = false;
    while (true) {
        std::optional<request_settings> next;
        uint64_t next_id = 0;
        bool refresh = false;
        {
            std::unique_lock lock(shared.mutex);
            if (!running)
                shared.changed.wait(lock, [&] {
                    return shared.shutdown || shared.pending.has_value() || shared.stop ||
                           shared.pause || shared.resume || (session && needs_frame(shared, *session));
                });
            if (shared.shutdown)
                return;
            if (shared.pending) {
                next = std::move(shared.pending);
                shared.pending.reset();
                next_id = shared.render_id;
                running = false;
            } else {
                if (shared.stop || shared.pause) {
                    shared.phase = shared.stop ? "stopped" : "paused";
                    shared.stop = shared.pause = shared.resume = false;
                    running = false;
                    if (session) {
                        session->pause();
                        shared.stats = session->current_stats();
                    }
                } else if (shared.resume) {
                    shared.resume = false;
                    if (session && shared.render_id == session->id) {
                        session->resume();
                        running = true;
                        shared.phase = "running";
                    }
                }
                refresh = !running && session && needs_frame(shared, *session);
            }
        }
        if (next) {
            session.reset();
            try {
                @autoreleasepool {
                    session.emplace(next_id, *next);
                }
                std::lock_guard lock(shared.mutex);
                if (shared.render_id == next_id) {
                    shared.phase = "running";
                    running = true;
                }
            } catch (const std::exception &e) {
                std::lock_guard lock(shared.mutex);
                if (shared.render_id == next_id) {
                    shared.phase = "error";
                    shared.error = e.what();
                    shared.pause = false;
                    shared.resume = false;
                }
                session.reset();
            }
            continue;
        }
        if (!session || (!running && !refresh))
            continue;

        try {
            @autoreleasepool {
                if (!running) {
                    // Paused or stopped: the GPU is idle, so capture and convert directly.
                    session->capture();
                    publish_frame(shared, *session);
                } else {
                    session->dispatch(random);
                    if (session->has_unconverted_capture())
                        publish_frame(shared, *session); // overlaps with the batch
                    session->finish();
                    bool capture = false;
                    {
                        std::lock_guard lock(shared.mutex);
                        if (shared.render_id == session->id && !shared.pending) {
                            shared.stats = session->current_stats();
                            // A pause or stop captures after the worker has seen it, above.
                            capture = !shared.stop && !shared.pause &&
                                      (shared.frame_render_id != session->id ||
                                       shared.frame_display_revision != shared.display_revision ||
                                       std::chrono::steady_clock::now() - session->last_capture >=
                                           std::chrono::seconds(1));
                        }
                    }
                    if (capture)
                        session->capture();
                }
            }
        } catch (const std::exception &e) {
            {
                std::lock_guard lock(shared.mutex);
                if (shared.render_id == session->id) {
                    shared.phase = "error";
                    shared.error = e.what();
                    shared.pause = false;
                    shared.resume = false;
                }
            }
            session.reset();
            running = false;
        }
    }
}

std::string escape_json(const std::string &value) {
    std::string out;
    for (char c : value) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
            continue;
        }
        out += c;
    }
    return out;
}

std::string status_json(state &shared) {
    std::lock_guard lock(shared.mutex);
    const session_stats &stats = shared.stats;
    std::ostringstream out;
    out << "{\"render_id\":" << shared.render_id
        << ",\"frame_render_id\":" << shared.frame_render_id
        << ",\"frame_revision\":" << shared.frame_revision
        << ",\"frame_samples\":" << shared.frame_samples << ",\"phase\":\"" << shared.phase
        << "\",\"display_revision\":" << shared.display_revision
        << ",\"frame_display_revision\":" << shared.frame_display_revision
        << ",\"display\":{\"brightness\":" << shared.display.brightness
        << ",\"contrast\":" << shared.display.contrast
        << ",\"saturation\":" << shared.display.saturation
        << ",\"clarity\":" << shared.display.clarity << ",\"texture\":" << shared.display.texture
        << "}"
        << ",\"samples\":" << stats.samples << ",\"elapsed\":" << stats.elapsed
        << ",\"width\":" << shared.output_width << ",\"height\":" << shared.output_height
        << ",\"cre\":" << shared.cre << ",\"cim\":" << shared.cim << ",\"scale\":" << shared.scale
        << ",\"batch_seconds\":" << stats.batch_seconds
        << ",\"capture_seconds\":" << stats.capture_seconds
        << ",\"capture_count\":" << stats.capture_count
        << ",\"preview_seconds\":" << stats.preview_seconds
        << ",\"preview_count\":" << stats.preview_count
        << ",\"recolor_seconds\":" << stats.recolor_seconds
        << ",\"recolor_count\":" << stats.recolor_count << ",\"error\":\""
        << escape_json(shared.error) << "\"}";
    return out.str();
}


struct http_request {
    std::string method, path, query, body;
    std::string host, origin;
};

http_request read_request(int fd) {
    std::string data;
    char buffer[4096];
    size_t header_end = std::string::npos;
    while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
        const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0)
            throw std::runtime_error("incomplete HTTP request");
        data.append(buffer, size_t(count));
        if (data.size() > 16'384)
            throw std::runtime_error("HTTP headers too large");
    }
    std::istringstream header(data.substr(0, header_end));
    http_request request;
    header >> request.method >> request.path;
    if (request.path.empty())
        throw std::runtime_error("invalid HTTP request line");
    const size_t query = request.path.find('?');
    if (query != std::string::npos) {
        request.query = request.path.substr(query + 1);
        request.path.resize(query);
    }
    size_t length = 0;
    std::string line;
    std::getline(header, line);
    while (std::getline(header, line)) {
        if (line.starts_with("Content-Length:") || line.starts_with("content-length:"))
            length = std::stoul(line.substr(line.find(':') + 1));
        if (line.starts_with("Host:") || line.starts_with("host:")) {
            request.host = line.substr(line.find(':') + 1);
            request.host.erase(0, request.host.find_first_not_of(' '));
            if (!request.host.empty() && request.host.back() == '\r')
                request.host.pop_back();
        }
        if (line.starts_with("Origin:") || line.starts_with("origin:")) {
            request.origin = line.substr(line.find(':') + 1);
            request.origin.erase(0, request.origin.find_first_not_of(' '));
            if (!request.origin.empty() && request.origin.back() == '\r')
                request.origin.pop_back();
        }
    }
    if (length > 4096)
        throw std::runtime_error("request body too large");
    request.body = data.substr(header_end + 4);
    while (request.body.size() < length) {
        const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0)
            throw std::runtime_error("incomplete HTTP body");
        request.body.append(buffer, size_t(count));
    }
    request.body.resize(length);
    return request;
}

void send_all(int fd, const uint8_t *bytes, size_t size) {
    while (size) {
        const ssize_t sent = send(fd, bytes, size, 0);
        if (sent <= 0)
            return;
        bytes += sent;
        size -= size_t(sent);
    }
}

void respond(int fd, int code, const std::string &type, const uint8_t *body, size_t size) {
    std::ostringstream head;
    head << "HTTP/1.1 " << code << (code == 200 ? " OK" : " Error") << "\r\n"
         << "Content-Type: " << type << "\r\n"
         << "Content-Length: " << size << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "Connection: close\r\n\r\n";
    const std::string text = head.str();
    send_all(fd, reinterpret_cast<const uint8_t *>(text.data()), text.size());
    send_all(fd, body, size);
}

void respond(int fd, int code, const std::string &type, const std::string &body) {
    respond(fd, code, type, reinterpret_cast<const uint8_t *>(body.data()), body.size());
}

void handle(int fd, state &shared, const std::string &html, uint16_t port) {
    try {
        const http_request request = read_request(fd);
        const std::string host = "127.0.0.1:" + std::to_string(port);
        if (request.host != host || (!request.origin.empty() && request.origin != "http://" + host))
            throw std::runtime_error("request origin is not the local UI");
        if (request.method == "GET" && request.path == "/") {
            respond(fd, 200, "text/html; charset=utf-8", html);
        } else if (request.method == "GET" && request.path == "/status") {
            respond(fd, 200, "application/json", status_json(shared));
        } else if (request.method == "GET" && request.path == "/frame.rgba") {
            std::shared_ptr<const std::vector<uint8_t>> frame;
            bool stale = false;
            {
                std::lock_guard lock(shared.mutex);
                frame = shared.frame;
                stale = !request.query.empty() &&
                    request.query != "revision=" + std::to_string(shared.frame_revision);
            }
            if (stale)
                respond(fd, 409, "text/plain", "preview revision changed");
            else if (!frame)
                respond(fd, 404, "text/plain", "no preview yet");
            else
                respond(fd, 200, "application/octet-stream", frame->data(), frame->size());
        } else if (request.method == "POST" && request.path == "/render") {
            const request_settings c = parse_settings(request.body);
            {
                std::lock_guard lock(shared.mutex);
                ++shared.render_id;
                shared.pending = c;
                shared.stop = false;
                shared.pause = false;
                shared.resume = false;
                shared.stats = {};
                shared.output_width = c.output_width;
                shared.output_height = c.output_height;
                shared.cre = c.cre;
                shared.cim = c.cim;
                shared.scale = c.scale;
                shared.phase = "starting";
                shared.error.clear();
            }
            shared.changed.notify_one();
            respond(fd, 200, "application/json", status_json(shared));
        } else if (request.method == "POST" && request.path == "/display") {
            const auto display = parse_display(request.body);
            {
                std::lock_guard lock(shared.mutex);
                shared.display = display;
                ++shared.display_revision;
            }
            shared.changed.notify_one();
            respond(fd, 200, "application/json", status_json(shared));
        } else if (request.method == "POST" && request.path == "/pause") {
            {
                std::lock_guard lock(shared.mutex);
                if (shared.phase != "running" && shared.phase != "starting")
                    throw std::invalid_argument("no active render to pause");
                shared.pause = true;
                shared.phase = "pausing";
            }
            shared.changed.notify_one();
            respond(fd, 200, "application/json", status_json(shared));
        } else if (request.method == "POST" && request.path == "/resume") {
            {
                std::lock_guard lock(shared.mutex);
                if (shared.phase != "paused")
                    throw std::invalid_argument("no paused render to resume");
                shared.resume = true;
                shared.phase = "resuming";
            }
            shared.changed.notify_one();
            respond(fd, 200, "application/json", status_json(shared));
        } else if (request.method == "POST" && request.path == "/stop") {
            {
                std::lock_guard lock(shared.mutex);
                shared.pending.reset();
                shared.stop = true;
                shared.pause = false;
                shared.resume = false;
                shared.phase = "stopping";
            }
            shared.changed.notify_one();
            respond(fd, 200, "application/json", status_json(shared));
        } else {
            respond(fd, 404, "text/plain", "not found");
        }
    } catch (const std::exception &e) {
        respond(fd, 400, "text/plain", e.what());
    }
}

int listen_local(uint16_t &port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw std::runtime_error("cannot create HTTP socket");
    const int enabled = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(fd, 16) != 0) {
        close(fd);
        throw std::runtime_error("cannot bind local HTTP socket");
    }
    socklen_t length = sizeof(address);
    getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length);
    port = ntohs(address.sin_port);
    return fd;
}

} // namespace

int main(int argc, char **argv) {
    @autoreleasepool {
        try {
            bool open_browser = true;
            if (argc == 2 && std::string(argv[1]) == "--no-open")
                open_browser = false;
            else if (argc != 1)
                throw std::invalid_argument("usage: buddha-browser [--no-open]");
            std::ifstream file(BUDDHA_BROWSER_HTML_PATH);
            if (!file)
                throw std::runtime_error("cannot read browser_prototype.html");
            const std::string html(std::istreambuf_iterator<char>(file), {});
            std::signal(SIGINT, [](int) { shutting_down = 1; });
            std::signal(SIGTERM, [](int) { shutting_down = 1; });
            uint16_t port = 0;
            const int server = listen_local(port);
            state shared;
            std::thread worker(render_worker, std::ref(shared));
            const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
            std::cout << "Buddha browser prototype: " << url << std::endl;
            if (open_browser)
                [[NSWorkspace sharedWorkspace]
                    openURL:[NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]]];
            while (!shutting_down) {
                fd_set sockets;
                FD_ZERO(&sockets);
                FD_SET(server, &sockets);
                timeval timeout{0, 250'000};
                if (select(server + 1, &sockets, nullptr, nullptr, &timeout) > 0) {
                    const int client = accept(server, nullptr, nullptr);
                    if (client >= 0) {
                        const int no_sigpipe = 1;
                        setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe,
                                   sizeof(no_sigpipe));
                        timeval limit{2, 0};
                        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
                        handle(client, shared, html, port);
                        close(client);
                    }
                }
            }
            close(server);
            {
                std::lock_guard lock(shared.mutex);
                shared.shutdown = true;
            }
            shared.changed.notify_one();
            worker.join();
            return 0;
        } catch (const std::exception &e) {
            std::cerr << "buddha-browser: " << e.what() << '\n';
            return 1;
        }
    }
}
