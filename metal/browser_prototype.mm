// Small local browser UI prototype: one Metal render and an HTTP RGBA preview.

#include "persistent_renderer.h"
#include "browser_display.h"

#include "buddha.h"
#include "settings.h"
#include "tone_mapping.h"

#import <AppKit/AppKit.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <boost/property_tree/json_parser.hpp>

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

constexpr uint64_t max_pixels = 2'000'000;
constexpr uint32_t batch_samples = 1U << 27;
volatile sig_atomic_t shutting_down = 0;

struct request_settings {
    uint32_t output_width, output_height;
    double cre, cim, scale;
    uint32_t lowr, lowg, lowb, highr, highg, highb;
};

request_settings parse_settings(const std::string &body) {
    std::istringstream input(body);
    boost::property_tree::ptree json;
    boost::property_tree::read_json(input, json);
    request_settings c{
        json.get<uint32_t>("width"), json.get<uint32_t>("height"), json.get<double>("cre"),
        json.get<double>("cim"),     json.get<double>("scale"),    json.get<uint32_t>("lowr"),
        json.get<uint32_t>("lowg"),  json.get<uint32_t>("lowb"),   json.get<uint32_t>("highr"),
        json.get<uint32_t>("highg"), json.get<uint32_t>("highb"),
    };
    if (c.output_width == 0 || c.output_height == 0 ||
        uint64_t(c.output_width) * c.output_height > max_pixels)
        throw std::invalid_argument("viewport exceeds the two-million-pixel prototype limit");
    if (!std::isfinite(c.cre) || !std::isfinite(c.cim) || !std::isfinite(c.scale) || c.scale <= 0)
        throw std::invalid_argument("center and scale must be finite; scale must be positive");
    if (c.highr == 0 || c.highg == 0 || c.highb == 0 || c.highr > 100'000 || c.highg > 100'000 ||
        c.highb > 100'000 || c.lowr >= c.highr || c.lowg >= c.highg || c.lowb >= c.highb)
        throw std::invalid_argument("invalid channel iteration ranges");
    return c;
}

buddha_browser::display_settings parse_display(const std::string &body) {
    std::istringstream input(body);
    boost::property_tree::ptree json;
    boost::property_tree::read_json(input, json);
    buddha_browser::display_settings display;
    display.brightness = json.get<int>("brightness");
    display.contrast = json.get<int>("contrast");
    display.saturation = json.get<int>("saturation");
    display.clarity = json.get<int>("clarity");
    display.texture = json.get<int>("texture");
    if (display.brightness < -100 || display.brightness > 100 || display.contrast < -100 ||
        display.contrast > 100 || display.saturation < -100 || display.saturation > 100 ||
        display.clarity < -100 || display.clarity > 100 || display.texture < -100 ||
        display.texture > 100)
        throw std::invalid_argument("display values are outside their supported ranges");
    return display;
}

settings make_settings(const request_settings &c) {
    settings s{};
    // The TIFF writer rotates the histogram 90 degrees clockwise.
    s.w = c.output_height;
    s.h = c.output_width;
    s.cre = c.cre;
    s.cim = c.cim;
    s.scale = c.scale;
    s.lowr = c.lowr;
    s.lowg = c.lowg;
    s.lowb = c.lowb;
    s.highr = c.highr;
    s.highg = c.highg;
    s.highb = c.highb;
    s.contrast = 100;
    s.lightness = 100;
    s.threads = 1;
    s.sampler = "naive";
    s.exclusion = BUDDHA_EXCLUSION_MAP;
    s.no_image = true;
    s.indirect_settings();
    return s;
}

std::vector<uint8_t> make_base_rgba(const buddha &b) {
    const settings &s = b.s;
    uint32_t maximum[3]{};
    for (size_t i = 0; i < b.raw.size(); i += 3)
        for (size_t channel = 0; channel < 3; ++channel)
            maximum[channel] = std::max(maximum[channel], b.raw[i + channel].load());
    float multiplier[3]{};
    for (size_t channel = 0; channel < 3; ++channel)
        multiplier[channel] =
            buddha_tone::multiplier(maximum[channel], s.scale, s.realContrast, s.realLightness);

    // Output coordinates match saver.h's clockwise TIFF transform exactly.
    const size_t width = s.h, height = s.w;
    std::vector<uint8_t> rgba(width * height * 4);
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const size_t index = buddha_tone::histogram_index(
                y, s.h - x - 1, s.w, s.h, s.symmetric_image, s.histogram_height);
            const size_t out = (y * width + x) * 4;
            for (size_t channel = 0; channel < 3; ++channel)
                rgba[out + channel] = buddha_tone::channel8(b.raw[index + channel].load(),
                                                            multiplier[channel], s.realContrast);
            rgba[out + 3] = 255;
        }
    }

    return rgba;
}

struct render_session {
    uint64_t id;
    std::unique_ptr<buddha> image;
    std::unique_ptr<buddha_metal::persistent_renderer> gpu;
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point last_preview;
    uint64_t samples = 0;
    double batch_seconds = 0;
    double preview_seconds = 0;
    uint64_t preview_count = 0;
    double recolor_seconds = 0;
    uint64_t recolor_count = 0;
    std::optional<uint64_t> base_samples;
    std::vector<uint8_t> base_rgba;
    double elapsed_before_pause = 0;
    bool paused = false;

    render_session(uint64_t render_id, const request_settings &c) : id(render_id) {
        if (!std::filesystem::is_regular_file(BUDDHA_EXCLUSION_MAP))
            throw std::runtime_error("default exclusion map is missing: " BUDDHA_EXCLUSION_MAP);
        image = std::make_unique<buddha>(make_settings(c));
        buddha_metal::check_histogram_layout<buddha::vector_type>();
        gpu = std::make_unique<buddha_metal::persistent_renderer>(
            buddha_metal::default_device(),
            buddha_metal::make_parameters(image->s, image->core.size), image->core.data.data(),
            image->core.data.size(), image->raw.data(), buddha_metal::histogram_bytes(image->raw));
        start = last_preview = std::chrono::steady_clock::now();
    }

    double elapsed() const {
        return elapsed_before_pause +
               (paused ? 0
                       : std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                             .count());
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

    void advance(std::random_device &random) {
        const auto begin = std::chrono::steady_clock::now();
        auto command = gpu->dispatch(0, batch_samples, random(), random());
        buddha_metal::persistent_renderer::wait(command);
        samples += batch_samples;
        batch_seconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    }

    std::shared_ptr<const std::vector<uint8_t>>
    capture_frame(const buddha_browser::display_settings &display) {
        const auto begin = std::chrono::steady_clock::now();
        const bool new_histogram = !base_samples || *base_samples != samples;
        if (new_histogram) {
            base_rgba = make_base_rgba(*image);
            base_samples = samples;
        }
        auto frame = std::make_shared<std::vector<uint8_t>>(base_rgba);
        buddha_browser::apply_display(*frame, image->s.h, image->s.w, display);
        const auto end = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(end - begin).count();
        if (new_histogram) {
            preview_seconds += seconds;
            ++preview_count;
        } else {
            recolor_seconds += seconds;
            ++recolor_count;
        }
        last_preview = end;
        return frame;
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
    uint64_t display_revision = 0;
    uint64_t frame_display_revision = 0;
    buddha_browser::display_settings display;
    uint64_t samples = 0;
    double elapsed = 0;
    double batch_seconds = 0;
    double preview_seconds = 0;
    uint64_t preview_count = 0;
    double recolor_seconds = 0;
    uint64_t recolor_count = 0;
    double cre = 0, cim = 0, scale = 0;
    uint32_t output_width = 0, output_height = 0;
    std::string phase = "idle";
    std::string error;
    std::shared_ptr<const std::vector<uint8_t>> frame;
};

void publish_frame(state &shared, render_session &session,
                   const buddha_browser::display_settings &display, uint64_t display_revision) {
    auto frame = session.capture_frame(display);
    std::lock_guard lock(shared.mutex);
    if (shared.render_id == session.id && !shared.pending &&
        shared.display_revision == display_revision) {
        shared.frame = std::move(frame);
        shared.frame_render_id = session.id;
        shared.frame_display_revision = display_revision;
        ++shared.frame_revision;
        shared.preview_seconds = session.preview_seconds;
        shared.preview_count = session.preview_count;
        shared.recolor_seconds = session.recolor_seconds;
        shared.recolor_count = session.recolor_count;
        if (shared.phase == "running")
            shared.elapsed = session.elapsed();
    }
}

void render_worker(state &shared) {
    std::optional<render_session> session;
    std::random_device random;
    bool running = false;
    while (true) {
        std::optional<request_settings> next;
        uint64_t next_id = 0;
        bool recolor = false;
        buddha_browser::display_settings display;
        uint64_t display_revision = 0;
        {
            std::unique_lock lock(shared.mutex);
            if (!running)
                shared.changed.wait(lock, [&] {
                    return shared.shutdown || shared.pending.has_value() || shared.stop ||
                           shared.pause || shared.resume ||
                           (session && shared.render_id == session->id &&
                            (shared.frame_render_id != session->id ||
                             shared.frame_display_revision != shared.display_revision));
                });
            if (shared.shutdown)
                return;
            if (shared.pending) {
                next = std::move(shared.pending);
                shared.pending.reset();
                next_id = shared.render_id;
                running = false;
            } else {
                if (shared.stop) {
                    shared.stop = false;
                    shared.pause = false;
                    shared.resume = false;
                    running = false;
                    shared.phase = "stopped";
                    if (session) {
                        session->pause();
                        shared.elapsed = session->elapsed();
                    }
                } else if (shared.pause) {
                    shared.pause = false;
                    running = false;
                    shared.phase = "paused";
                    if (session) {
                        session->pause();
                        shared.elapsed = session->elapsed();
                    }
                } else if (shared.resume) {
                    shared.resume = false;
                    if (session && shared.render_id == session->id) {
                        session->resume();
                        running = true;
                        shared.phase = "running";
                    }
                }
                if (!running && session && shared.render_id == session->id &&
                    (shared.frame_render_id != session->id ||
                     shared.frame_display_revision != shared.display_revision)) {
                    recolor = true;
                    display = shared.display;
                    display_revision = shared.display_revision;
                }
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
        if (recolor) {
            try {
                @autoreleasepool {
                    publish_frame(shared, *session, display, display_revision);
                }
            } catch (const std::exception &e) {
                std::lock_guard lock(shared.mutex);
                shared.phase = "error";
                shared.error = e.what();
                shared.pause = false;
                shared.resume = false;
                session.reset();
            }
            continue;
        }
        if (!running || !session)
            continue;

        try {
            @autoreleasepool {
                session->advance(random);
                bool preview = false;
                {
                    std::lock_guard lock(shared.mutex);
                    if (shared.render_id == session->id && !shared.pending) {
                        shared.samples = session->samples;
                        shared.elapsed = session->elapsed();
                        shared.batch_seconds = session->batch_seconds;
                        shared.preview_seconds = session->preview_seconds;
                        shared.preview_count = session->preview_count;
                        shared.recolor_seconds = session->recolor_seconds;
                        shared.recolor_count = session->recolor_count;
                        preview = shared.stop || shared.pause ||
                                  shared.frame_render_id != session->id ||
                                  shared.frame_display_revision != shared.display_revision ||
                                  std::chrono::steady_clock::now() - session->last_preview >=
                                      std::chrono::seconds(1);
                        if (preview) {
                            display = shared.display;
                            display_revision = shared.display_revision;
                        }
                    }
                }
                if (preview)
                    publish_frame(shared, *session, display, display_revision);
            }
        } catch (const std::exception &e) {
            std::lock_guard lock(shared.mutex);
            if (shared.render_id == session->id) {
                shared.phase = "error";
                shared.error = e.what();
                shared.pause = false;
                shared.resume = false;
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
    std::ostringstream out;
    out << "{\"render_id\":" << shared.render_id
        << ",\"frame_render_id\":" << shared.frame_render_id
        << ",\"frame_revision\":" << shared.frame_revision << ",\"phase\":\"" << shared.phase
        << "\",\"display_revision\":" << shared.display_revision
        << ",\"frame_display_revision\":" << shared.frame_display_revision
        << ",\"display\":{\"brightness\":" << shared.display.brightness
        << ",\"contrast\":" << shared.display.contrast
        << ",\"saturation\":" << shared.display.saturation
        << ",\"clarity\":" << shared.display.clarity << ",\"texture\":" << shared.display.texture
        << "}"
        << ",\"samples\":" << shared.samples << ",\"elapsed\":" << shared.elapsed
        << ",\"width\":" << shared.output_width << ",\"height\":" << shared.output_height
        << ",\"cre\":" << shared.cre << ",\"cim\":" << shared.cim << ",\"scale\":" << shared.scale
        << ",\"batch_seconds\":" << shared.batch_seconds
        << ",\"preview_seconds\":" << shared.preview_seconds
        << ",\"preview_count\":" << shared.preview_count
        << ",\"recolor_seconds\":" << shared.recolor_seconds
        << ",\"recolor_count\":" << shared.recolor_count << ",\"error\":\""
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
                shared.samples = 0;
                shared.elapsed = 0;
                shared.batch_seconds = 0;
                shared.preview_seconds = 0;
                shared.preview_count = 0;
                shared.recolor_seconds = 0;
                shared.recolor_count = 0;
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
