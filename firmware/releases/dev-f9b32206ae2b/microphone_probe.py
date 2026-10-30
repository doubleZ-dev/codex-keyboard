"""Explicit local capture from the Codex board microphone; never selects the PC default mic."""
import argparse
from array import array
import json
import math
import time
from pathlib import Path
import wave


def pcm_stats(data):
    samples = array('h')
    samples.frombytes(data)
    if not samples:
        raise ValueError('No PCM samples')
    mean = sum(samples) / len(samples)
    rms = math.sqrt(sum(value * value for value in samples) / len(samples))
    return dict(samples=len(samples), peak=max(abs(v) for v in samples),
                rms=round(rms, 3), dc=round(mean, 3),
                clipped=sum(v in (-32768, 32767) for v in samples),
                all_zero=not any(samples))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--device', type=int)
    parser.add_argument('--record', type=Path)
    parser.add_argument('--seconds', type=int, default=5, choices=range(1, 31))
    parser.add_argument('--selftest', action='store_true')
    args = parser.parse_args()
    if args.selftest:
        assert pcm_stats(array('h', [0] * 48).tobytes())['all_zero']
        stats = pcm_stats(array('h', [-32768, -1000, 0, 1000, 32767]).tobytes())
        assert stats['peak'] == 32768 and stats['clipped'] == 2
        print('PCM statistics self-test passed; no audio devices accessed')
        return
    if not args.list and not args.record:
        parser.error('Choose --list or --record FILE --device INDEX')
    import pyaudio
    audio = pyaudio.PyAudio()
    try:
        if args.list:
            for index in range(audio.get_device_count()):
                info = audio.get_device_info_by_index(index)
                if info['maxInputChannels']:
                    print(json.dumps(dict(index=index, name=info['name'],
                        host_api=audio.get_host_api_info_by_index(info['hostApi'])['name'],
                        channels=info['maxInputChannels']), ensure_ascii=False))
        if args.record:
            if args.device is None:
                parser.error('An explicit --device INDEX is required; PC default mic is never used')
            info = audio.get_device_info_by_index(args.device)
            if not any(name in info['name'].casefold() for name in ('codex micro', 'inmp441')):
                raise ValueError('Selected input is not identified as the Codex board microphone')
            chunks = []
            flags = []
            target = args.seconds * 48000 * 2
            captured = 0
            def callback(in_data, frame_count, time_info, status_flags):
                nonlocal captured
                if status_flags:
                    flags.append(status_flags)
                chunks.append(in_data)
                captured += len(in_data)
                return (None, pyaudio.paComplete if captured >= target else pyaudio.paContinue)
            stream = audio.open(format=pyaudio.paInt16, channels=1, rate=48000,
                input=True, input_device_index=args.device, frames_per_buffer=480,
                stream_callback=callback)
            try:
                deadline = time.monotonic() + args.seconds + 5
                while stream.is_active() and time.monotonic() < deadline:
                    time.sleep(0.05)
                if captured < target:
                    raise TimeoutError(f'Audio stalled: received {captured} of {target} bytes')
                if flags:
                    raise RuntimeError(f'Audio callback reported errors: {flags}')
            finally:
                stream.stop_stream()
                stream.close()
            data = b''.join(chunks)[:target]
            with wave.open(str(args.record), 'wb') as out:
                out.setnchannels(1)
                out.setsampwidth(2)
                out.setframerate(48000)
                out.writeframes(data)
            print(json.dumps(dict(device=info['name'], path=str(args.record.resolve()),
                                  **pcm_stats(data)), ensure_ascii=False))
    finally:
        audio.terminate()


if __name__ == '__main__':
    main()
