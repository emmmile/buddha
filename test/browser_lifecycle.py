#!/usr/bin/env python3
"""buddha-browser's render worker over HTTP: start, restart, pause, display changes, resume, stop,
and frame building without a polling client. Exits 77 (skipped) without a Metal device.

usage: browser_lifecycle.py path/to/buddha-browser
"""
import json
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

WIDTH, HEIGHT = 320, 200
TIMEOUT = 60  # seconds; generous for slow virtual GPUs
RENDER = dict(width=WIDTH, height=HEIGHT, cre=-0.5, cim=0, scale=60,
              lowr=512, highr=8192, lowg=128, highg=2048, lowb=32, highb=512)
DISPLAY = dict(brightness=25, contrast=10, saturation=50, clarity=50, texture=50)


class Failure(Exception):
    pass


def check(condition, message):
    if not condition:
        raise Failure(message)


class Server:
    def __init__(self, binary):
        self.process = subprocess.Popen([binary, '--no-open'], stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, text=True)
        self.host = self.process.stdout.readline().split()[-1].rstrip('/')
        self.last = None  # the previous status, for monotonicity checks

    def request(self, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        request = urllib.request.Request(self.host + path, data=data,
                                         method='GET' if data is None else 'POST',
                                         headers={'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as error:
            return error.code, error.read()

    def post(self, path, body=None):
        code, data = self.request(path, {} if body is None else body)
        check(code == 200, f'{path} returned {code}: {data[:200]!r}')
        return json.loads(data)

    def status(self):
        code, data = self.request('/status')
        check(code == 200, f'/status returned {code}')
        s = json.loads(data)
        if s['phase'] == 'error':
            if 'Metal device' in s['error']:
                print('skipped: ' + s['error'])
                sys.exit(77)
            raise Failure('render error: ' + s['error'])
        if self.last:
            check(s['frame_revision'] >= self.last['frame_revision'], 'frame revision went back')
        if self.last and self.last['render_id'] == s['render_id']:
            check(s['batches'] >= self.last['batches'], 'batch count went back')
        if self.last and s['frame_render_id'] != self.last['frame_render_id']:
            check(s['frame_render_id'] > self.last['frame_render_id'],
                  f"a frame from render {s['frame_render_id']} followed render "
                  f"{self.last['frame_render_id']}")
        self.last = s
        return s

    def until(self, predicate, what):
        end = time.monotonic() + TIMEOUT
        while time.monotonic() < end:
            s = self.status()
            if predicate(s):
                return s
            time.sleep(0.02)
        raise Failure('timed out waiting for ' + what)

    def frame(self, s):
        code, data = self.request(f"/frame.rgba?revision={s['frame_revision']}")
        if code == 409:  # a newer frame was published in between
            return None
        check(code == 200, f'/frame.rgba returned {code}')
        check(len(data) == WIDTH * HEIGHT * 4, f'frame has {len(data)} bytes')
        return data

    def stop(self):
        self.process.send_signal(signal.SIGTERM)
        try:
            return self.process.wait(timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            self.process.kill()
            raise Failure('server did not exit after SIGTERM')


def current(s):
    """The published frame shows the latest completed batch with the current display."""
    return (s['frame_render_id'] == s['render_id'] and s['frame_batches'] == s['batches']
            and s['frame_display_revision'] == s['display_revision'])


def lifecycle(server):
    check(server.request('/pause', {})[0] == 400, 'pause without a render was accepted')
    check(server.request('/render', dict(RENDER, width=0))[0] == 400, 'empty viewport accepted')

    first = server.post('/render', RENDER)['render_id']
    s = server.until(lambda s: s['frame_render_id'] == first and s['phase'] == 'running',
                     'the first frame')
    server.frame(s)
    server.until(lambda s: s['batches'] > s['frame_batches'] > 0, 'sampling past the frame')

    # Restart while running: the old frame stays until the new render's first frame, and no
    # frame of the old render follows it (Server.status checks the order).
    second = server.post('/render', dict(RENDER, cim=0.1))['render_id']
    check(second == first + 1, 'render ids are not consecutive')
    s = server.until(lambda s: s['frame_render_id'] == second, 'the restarted render frame')
    check(s['frame_batches'] <= s['batches'], 'frame is ahead of the histogram')
    server.frame(s)

    # Display change while running: a frame with the new display follows.
    revision = server.post('/display', dict(DISPLAY, brightness=60))['display_revision']
    server.until(lambda s: s['frame_display_revision'] == revision, 'a running display frame')

    # Pause: sampling stops and the frame shows the final histogram.
    server.post('/pause')
    s = server.until(lambda s: s['phase'] == 'paused' and current(s), 'a paused final frame')
    time.sleep(0.5)
    check(server.status()['batches'] == s['batches'], 'batches grew while paused')

    # Display change while paused: a recolor, with no new histogram and no new samples.
    before = server.status()
    revision = server.post('/display', dict(DISPLAY, saturation=-20))['display_revision']
    s = server.until(lambda s: s['frame_display_revision'] == revision, 'a paused recolor')
    check(s['batches'] == before['batches'], 'a display change added batches')
    check(s['preview_count'] == before['preview_count'], 'a recolor rebuilt the base image')
    check(s['recolor_count'] > before['recolor_count'], 'a recolor was not counted')
    server.frame(s)
    check(server.request('/display', dict(DISPLAY, texture=101))[0] == 400,
          'out-of-range display accepted')

    # Resume: sampling continues from the same histogram.
    server.post('/resume')
    server.until(lambda s: s['phase'] == 'running' and s['batches'] > before['batches'],
                 'batches after resume')

    # Without a polling client the worker stops building frames. The status reads below mark
    # the client active for 3 s each, which allows at most a few captures a second apart.
    start = server.status()
    time.sleep(12)
    end = server.status()
    batches = end['batches'] - start['batches']
    captures = end['capture_count'] - start['capture_count']
    check(captures <= 5, f'{captures} captures in 12 s without a client ({batches} batches)')
    s = server.until(lambda s: s['frame_revision'] > end['frame_revision'],
                     'a frame once the client returns')

    # Stop: the final histogram is shown, sampling ends, and a stopped render cannot resume.
    server.post('/stop')
    s = server.until(lambda s: s['phase'] == 'stopped' and current(s), 'a stopped final frame')
    check(server.request('/resume', {})[0] == 400, 'a stopped render resumed')
    time.sleep(0.5)
    idle = server.status()
    check(idle['batches'] == s['batches'], 'batches grew after stop')
    check(idle['frame_revision'] == s['frame_revision'], 'frames were built after stop')

    # Starting again creates a new histogram.
    third = server.post('/render', RENDER)['render_id']
    s = server.until(lambda s: s['frame_render_id'] == third, 'a frame after stop and start')
    check(s['frame_batches'] <= s['batches'] and s['render_id'] == third, 'new render state')
    check(s['orbits'] > 0 and 0 < s['drawn'] <= s['orbits'] and s['steps'] > 0 and s['points'] > 0,
          'common sampler metrics')
    check([m['name'] for m in s['sampler_metrics']] == ['excluded'], 'naive sampler metrics')

    # Metropolis reports the same common metrics, then its own.
    fourth = server.post('/render', dict(RENDER, sampler='metropolis'))['render_id']
    s = server.until(lambda s: s['frame_render_id'] == fourth and s['batches'] > 0,
                     'a Metropolis frame')
    check(s['sampler'] == 'metropolis' and s['orbits'] > 0 and s['points'] > 0,
          'common Metropolis metrics')
    check([(m['name'], m['primary']) for m in s['sampler_metrics']] ==
          [('accepted', True), ('chain', False)], 'Metropolis sampler metrics')
    check(server.request('/render', dict(RENDER, sampler='metropolis', seeding='x'))[0] == 400,
          'an unknown seeding accepted')


def main():
    server = Server(sys.argv[1])
    try:
        lifecycle(server)
    except Failure as failure:
        print('FAILED: ' + str(failure))
        server.stop()
        return 1
    check(server.stop() == 0, 'server exited with an error')
    print('ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
