#!/usr/bin/env python3
"""Solve JUCE ParameterID strings that produce chosen VST3 parameter IDs.

JUCE derives a VST3 param ID as `String::hashCode() & 0x7fffffff`, where
hashCode is `result = 31 * result + char` accumulated in uint32
(juce_audio_plugin_client_VST3.cpp). 31 is odd, so the hash is invertible
mod 2^32 and any target is reachable.

Incrementing the last character increments the hash by exactly 1, so a single
prefix yields a consecutive run of IDs -- which is what a bank of macro
parameters needs.

Usage:
  solve_param_ids.py 0x08eaca05          # find a prefix, print the run of 8
  solve_param_ids.py 0x08eaca05 --count 10
  solve_param_ids.py --verify Jlkb~q     # show what a known prefix produces

Verified: this reproduces "Jlkb~q" for OSCpar's 0x08eaca05..0x08eaca0c, and
hash("macro1") & 0x7fffffff == 0x3f85dde5 as observed in real project bytes.
"""
import sys

MASK = 0xFFFFFFFF
LO, HI = 32, 126          # printable ASCII
M = 31


def h32(s):
    r = 0
    for c in s:
        r = (M * r + ord(c)) & MASK
    return r


def param_id(s):
    return h32(s) & 0x7FFFFFFF


def _powsum(n):
    return sum(M ** i for i in range(n))


def _digits(X, n):
    """chars c_0..c_{n-1} in [LO,HI] with sum c_i * 31^(n-1-i) == X."""
    out = []

    def go(i, rem):
        if i == n:
            return rem == 0
        w = M ** (n - 1 - i)
        rest = n - 1 - i
        lo_rest, hi_rest = LO * _powsum(rest), HI * _powsum(rest)
        if rem < lo_rest:
            return False
        c_hi = min(HI, (rem - lo_rest) // w)
        c_lo = max(LO, -(-(rem - hi_rest) // w))
        for c in range(c_lo, c_hi + 1):
            out.append(chr(c))
            if go(i + 1, rem - c * w):
                return True
            out.pop()
        return False

    return ''.join(out) if go(0, X) else None


def solve(target, first='1'):
    """Find a prefix P where param_id(P + first) == target."""
    inv31 = pow(M, -1, 1 << 32)
    for signbit in (0, 1 << 31):
        for k in range(4):
            t = (target | signbit) + k * (1 << 32)
            X = ((t - ord(first)) * inv31) % (1 << 32)
            for n in (4, 5, 6, 7):
                for kk in range(3):
                    s = _digits(X + kk * (1 << 32), n)
                    if s is not None and param_id(s + first) == target:
                        return s
    return None


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1

    if args[0] == '--verify':
        prefix = args[1]
        for d in range(1, 9):
            print(f'  "{prefix}{d}" -> 0x{param_id(prefix + str(d)):08x}')
        return 0

    target = int(args[0], 0)
    count = int(args[args.index('--count') + 1]) if '--count' in args else 8

    prefix = solve(target)
    if prefix is None:
        print(f"no prefix found for 0x{target:08x}")
        return 1

    print(f'prefix {prefix!r}')
    for d in range(1, count + 1):
        s = prefix + str(d)
        print(f'  ParameterID {s!r:<12} -> 0x{param_id(s):08x}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
