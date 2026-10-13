"""
Fault matrix: break the virtual board every way it can break, and check that
the firmware tells the truth about each one.

Every fault is injected from outside the firmware while the same binary keeps
running - a trace cut or two wires swapped in the live netlist, a chip pulled,
mis-strapped or cycled, a part at its datasheet limits. Nothing is rebuilt and
nothing is mocked.

The invariant across all of them: `adcregs` shows the right number or `---`.
Never a confident wrong number - except where a real board could not know
either, and then a test has to catch it, and does.

Each case says whether real hardware could detect the same fault. Some can
only be seen because the virtual PCB knows what copper cannot tell you; those
are marked `twin-only`, and they are the honest limit of the parity between
this board and the real one. See docs/hardware.md for the physical rig.

Runs only against the virtual PCB (configs/vpcb.yaml): you cannot cut a trace
on the bench thirty times and get it back.
"""

import re

import pytest

from test_adc import wait_for_fresh_sample

# Distinct value per channel, so a swapped or misrouted wire is visible.
VALUES = {ch: 200 + 190 * ch for ch in range(15)}

# Ideal parts: DAC code quantisation plus the ADC's 12-bit step - observed
# 2 mV at most (2000 mV reads back as 1998).
IDEAL_TOL_MV = 4

# A part at its datasheet limits, from the DAC7578 electrical characteristics:
# offset error +/-4 mV max, gain error +/-0.15 % of full scale (3.3 V), plus
# the same quantisation as above.
DATASHEET_TOL_MV = 4 + 0.0015 * 3300 + IDEAL_TOL_MV

CHIPS = {"0x48": range(0, 8), "0x4c": range(8, 15)}


# ------------------------------------------------------------------ helpers

def read_channels(dut):
    """One fresh sample, as {ch: mV}, with None for a channel showing ---."""
    wait_for_fresh_sample(dut)
    out = dut.send_command("adcregs")
    regs = {}
    for ch, val in re.findall(r"ch\[(\d+)\]:\s*(---|-?\d+)", out):
        regs[int(ch)] = None if val == "---" else int(val)
    assert len(regs) == 15, f"could not parse adcregs:\n{out}"
    return regs


def drive(dut, channels, mv=None):
    for ch in channels:
        resp = dut.send_command(f"dacset {ch} {VALUES[ch] if mv is None else mv}")
        assert "OK" in resp, f"dacset {ch} failed on a healthy chip: {resp!r}"


def assert_close(regs, ch, want, tol=IDEAL_TOL_MV):
    got = regs[ch]
    assert got is not None, f"ch[{ch}] shows --- but should read {want} mV"
    assert abs(got - want) <= tol, f"ch[{ch}] = {got} mV, want {want} +/- {tol}"


def assert_others_healthy(regs, skip):
    for ch in range(15):
        if ch not in skip:
            assert_close(regs, ch, VALUES[ch])


def without(lines, net):
    """The netlist with the trace to `net` cut."""
    return [l for l in lines if int(l.split()[2]) != net]


def swapped(lines, a, b):
    """The netlist with the traces to nets `a` and `b` crossed."""
    swap = {str(a): str(b), str(b): str(a)}
    out = []
    for l in lines:
        addr, chan, net = l.split()
        out.append(f"{addr} {chan} {swap.get(net, net)}")
    return out


def readdressed(lines, old, new):
    """The same copper, but the chip on it now answers at `new`."""
    return [l.replace(old, new, 1) if l.split()[0] == old else l for l in lines]


LIMITS = [("--offset-uv", "4000"), ("--offset-uv", "-4000"),
          ("--gain-ppm", "1500"), ("--gain-ppm", "-1500")]


# ----------------------------------------------------------------- fixtures

@pytest.fixture(scope="class")
def rig(test_config, request):
    # Decide before asking for the DUT, so other configs skip without booting one.
    if test_config.get("dut", {}).get("type") != "native_sim":
        pytest.skip("fault injection needs the virtual PCB (configs/vpcb.yaml)")
    dut = request.getfixturevalue("dut")
    drive(dut, range(15))
    return dut


@pytest.fixture
def board(rig):
    """The rig, put back exactly as it was after each case."""
    state = {"chips_touched": False}
    yield rig, state
    rig.rewire(None)
    if state["chips_touched"]:
        for addr in rig.restore_ics():
            drive(rig, CHIPS[addr])


# --------------------------------------------------------------- the matrix

class TestFaultMatrix:
    """Every case shares one running firmware; only the hardware changes."""

    @pytest.mark.parametrize("ch", range(15), ids=lambda c: f"ch{c:02d}-twin-only")
    def test_cut_trace(self, board, ch):
        """
        One trace cut. The DAC still accepts the write; nothing drives the pin.

        twin-only: on copper the pin floats and the ADC converts whatever it
        picks up, with nothing to say it is meaningless. Here the board knows
        the net is undriven, and the reading comes back as --- instead.
        """
        dut, _ = board
        dut.rewire(without(dut.netlist_lines(), ch))
        regs = read_channels(dut)
        assert regs[ch] is None, f"cut trace on ch[{ch}] still reads {regs[ch]} mV"
        assert_others_healthy(regs, skip={ch})

    @pytest.mark.parametrize("a,b", [(2, 3), (7, 8), (13, 14)],
                             ids=["ch02-ch03-bench", "ch07-ch08-bench", "ch13-ch14-bench"])
    def test_swapped_wires(self, board, a, b):
        """
        Two traces crossed. Every layer reports success and every reading is a
        real voltage - just on the wrong channel. 7/8 crosses between chips.

        bench: yes, and only this way - drive distinct values and check each
        lands where it should. No status flag can catch a crossed wire.
        """
        dut, _ = board
        dut.rewire(swapped(dut.netlist_lines(), a, b))
        regs = read_channels(dut)
        assert_close(regs, a, VALUES[b])
        assert_close(regs, b, VALUES[a])
        assert_others_healthy(regs, skip={a, b})

    @pytest.mark.parametrize("addr", list(CHIPS), ids=lambda a: f"{a}-bench-partial")
    def test_chip_missing(self, board, addr):
        """
        A chip pulled from the board.

        bench, partly: the failed write (-19, nothing acknowledged) is exactly
        the same on hardware. The --- on its channels is twin-only.
        """
        dut, state = board
        state["chips_touched"] = True
        dut.kill_ic(addr)

        resp = dut.send_command(f"dacset {CHIPS[addr][0]} 1000")
        assert "FAILED (-19)" in resp, f"write to a missing chip did not NAK: {resp!r}"

        regs = read_channels(dut)
        for ch in CHIPS[addr]:
            assert regs[ch] is None, f"ch[{ch}] reads {regs[ch]} mV with its chip gone"
        assert_others_healthy(regs, skip=set(CHIPS[addr]))

    @pytest.mark.parametrize("addr,wrong", [("0x4c", "0x4d"), ("0x48", "0x4a")],
                             ids=["0x4c-as-0x4d-bench", "0x48-as-0x4a-bench"])
    def test_wrong_strap(self, board, addr, wrong):
        """
        A chip strapped to the wrong address - ADDR0 tied high instead of low,
        or ADDR1 high instead of low. The strap table is in docs/hardware.md.

        The strap moves the chip's address, not its wiring: it is still
        soldered to the same traces, so the netlist follows it. Its outputs sit
        at power-on zero-scale, so its channels read a REAL 0 mV - a confident
        number that is the truth about the pin - while writes to the address
        the firmware expects go unanswered.

        bench: yes, identically - -19 on the write, 0 V on the pins.
        """
        dut, state = board
        state["chips_touched"] = True
        dut.kill_ic(addr)
        dut.start_ic(wrong)
        dut.rewire(readdressed(dut.netlist_lines(), addr, wrong))

        resp = dut.send_command(f"dacset {CHIPS[addr][0]} 1000")
        assert "FAILED (-19)" in resp, f"write to a mis-strapped chip did not NAK: {resp!r}"

        regs = read_channels(dut)
        for ch in CHIPS[addr]:
            assert_close(regs, ch, 0)
        assert_others_healthy(regs, skip=set(CHIPS[addr]))

    @pytest.mark.parametrize("flag,value", LIMITS,
                             ids=[f"{f.strip('-')}{v}-bench" for f, v in LIMITS])
    def test_part_at_datasheet_limit(self, board, flag, value):
        """
        A DAC exactly as bad as its datasheet allows. Not a fault: a part you
        are allowed to be shipped, which the test budget has to absorb.

        Measured near full scale, where a 0.15 % gain error is big enough to see
        above the 12-bit step, as a shift against the same chip when ideal.

        bench: yes - real parts land somewhere inside these limits.
        """
        dut, state = board
        state["chips_touched"] = True
        level = 3000

        drive(dut, CHIPS["0x48"], mv=level)
        ideal = read_channels(dut)

        dut.kill_ic("0x48")
        dut.start_ic("0x48", flag, value)
        drive(dut, CHIPS["0x48"], mv=level)
        regs = read_channels(dut)

        shift = sum(regs[ch] - ideal[ch] for ch in CHIPS["0x48"]) / len(CHIPS["0x48"])
        n = int(value)
        expected = n / 1000 if flag == "--offset-uv" else level * n / 1e6
        assert abs(shift - expected) <= 1.5, (
            f"{flag} {value}: readings moved {shift:+.1f} mV, "
            f"the datasheet error is {expected:+.1f} mV")
        for ch in CHIPS["0x48"]:
            assert_close(regs, ch, level, tol=DATASHEET_TOL_MV)
        assert_others_healthy(regs, skip=set(CHIPS["0x48"]))

    def test_intermittent_chip(self, board):
        """
        A chip that keeps dropping out - a cracked joint, a brown-out.

        While it is gone, each of its channels must read ---: nothing drives
        them. Once it is back it re-powers at zero-scale, so a real 0 mV is
        the truth. A 0 mV while it is gone is the firmware inventing a
        reading - and is exactly what it used to do.

        bench, partly: the write failures are real; the --- is twin-only.
        """
        dut, state = board
        state["chips_touched"] = True

        for _ in range(4):
            dut.kill_ic("0x48")
            gone = read_channels(dut)
            for ch in CHIPS["0x48"]:
                assert gone[ch] is None, f"ch[{ch}] invented {gone[ch]} mV with its chip gone"
            assert_others_healthy(gone, skip=set(CHIPS["0x48"]))

            dut.start_ic("0x48")
            back = read_channels(dut)
            for ch in CHIPS["0x48"]:
                assert_close(back, ch, 0)
            assert_others_healthy(back, skip=set(CHIPS["0x48"]))

        drive(dut, CHIPS["0x48"])
        assert_others_healthy(read_channels(dut), skip=set())

    def test_intermittent_trace(self, board):
        """
        A trace that opens and closes - a hairline crack flexing.

        ch[5] must read its value or ---, every time, never anything else.

        twin-only: copper would read the floating pin as some number.
        """
        dut, _ = board
        intact = dut.netlist_lines()
        for _ in range(4):
            dut.rewire(without(intact, 5))
            assert read_channels(dut)[5] is None
            dut.rewire(intact)
            assert_close(read_channels(dut), 5, VALUES[5])
