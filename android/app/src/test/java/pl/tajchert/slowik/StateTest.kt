package pl.tajchert.slowik

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import pl.tajchert.slowik.stream.Receiver
import pl.tajchert.slowik.stream.StageState
import pl.tajchert.slowik.stream.deriveStageState
import pl.tajchert.slowik.stream.parseReceiver

class StateTest {
    private fun s(streaming: Boolean = true, conn: Boolean = true, live: Boolean = false, op: Boolean = false, local: Boolean = false) =
        deriveStageState(streaming, conn, live, op, local)

    @Test fun noReceiverWhenNotConnected() {
        assertEquals(StageState.NO_RECEIVER, s(conn = false, live = true))
        assertEquals(StageState.NO_RECEIVER, s(streaming = false))
    }

    @Test fun readyLiveMuted() {
        assertEquals(StageState.READY, s())
        assertEquals(StageState.LIVE, s(live = true))
        assertEquals(StageState.MUTED, s(live = true, op = true))
        assertEquals(StageState.MUTED, s(local = true))
    }

    @Test fun parseReceiverForms() {
        assertEquals(Receiver("192.168.1.5", "192.168.1.5", 47800), parseReceiver("192.168.1.5"))
        assertEquals(9000, parseReceiver(" 10.0.0.2:9000 ")?.port)
        assertEquals("10.0.0.2", parseReceiver("slowik://10.0.0.2:47800")?.host)
        assertNull(parseReceiver(""))
        assertNull(parseReceiver("10.0.0.2:abc"))
        assertNull(parseReceiver("10.0.0.2:70000"))
        assertNull(parseReceiver("a b"))
    }

    @Test fun preflightFlagsProblems() {
        val ok = evaluatePreflight(true, true, 80, true, true)
        assert(ok.all { it.ok })
        val bad = evaluatePreflight(false, false, 40, false, false)
        assert(bad.none { it.ok })
        assertEquals(false, bad.first { it.id == CheckId.BATTERY }.ok)
    }
}
