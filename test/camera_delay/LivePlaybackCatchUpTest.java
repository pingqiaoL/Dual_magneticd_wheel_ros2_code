package com.climbwheel.mk32.video;

import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public final class LivePlaybackCatchUpTest {
    @Test public void drainsBacklogAndRetainsJitterMargin() {
        LivePlaybackCatchUp control = new LivePlaybackCatchUp();
        double bufferedMs = 625;
        boolean caughtUp = false;
        for (long nowMs = 0; nowMs < 12000; nowMs += 100) {
            float speed = control.update(Math.round(bufferedMs), true, nowMs);
            assertTrue(speed >= 1f && speed <= 1.20f);
            bufferedMs += 100 - 100 * speed;
            assertTrue(bufferedMs >= LivePlaybackCatchUp.TARGET_BUFFER_MS - 20);
            if (bufferedMs <= LivePlaybackCatchUp.TARGET_BUFFER_MS) {
                caughtUp = true;
                assertEquals(1f, control.update(Math.round(bufferedMs), true, nowMs + 100), 0f);
                break;
            }
        }
        assertTrue("Backlog should converge within twelve seconds", caughtUp);
    }

    @Test public void shortBurstRestartsTheFullHoldPeriod() {
        LivePlaybackCatchUp control = new LivePlaybackCatchUp();
        assertEquals(1f, control.update(625, true, 0), 0f);
        assertEquals(1f, control.update(625, true, 100), 0f);
        assertEquals(1f, control.update(LivePlaybackCatchUp.TARGET_BUFFER_MS + 20, true, 150), 0f);
        assertEquals(1f, control.update(625, true, 300), 0f);
        assertEquals(1f, control.update(625, true, 400), 0f);
        assertEquals(1.20f, control.update(625, true, 500), 0f);
    }

    @Test public void keepsCatchingUpBelowStartUntilTarget() {
        LivePlaybackCatchUp control = new LivePlaybackCatchUp();
        control.update(625, true, 0);
        assertEquals(1.20f, control.update(625, true, 200), 0f);
        assertEquals(1.10f, control.update(LivePlaybackCatchUp.TARGET_BUFFER_MS + 30, true, 300), 0f);
        assertEquals(1f, control.update(LivePlaybackCatchUp.TARGET_BUFFER_MS, true, 400), 0f);
        assertEquals(1f, control.update(LivePlaybackCatchUp.TARGET_BUFFER_MS + 30, true, 500), 0f);
    }

    @Test public void stalledFlowStopsAccelerationAndResetsHold() {
        LivePlaybackCatchUp control = new LivePlaybackCatchUp();
        control.update(625, true, 0);
        assertEquals(1.20f, control.update(625, true, 200), 0f);
        assertEquals(1f, control.update(625, false, 300), 0f);
        assertEquals(1f, control.update(625, true, 400), 0f);
        assertEquals(1f, control.update(625, true, 500), 0f);
    }

    @Test public void invalidBufferAndReconnectDiscardPreviousState() {
        LivePlaybackCatchUp control = new LivePlaybackCatchUp();
        control.update(625, true, 0);
        control.update(625, true, 200);
        assertEquals(1f, control.update(-1, true, 300), 0f);
        assertEquals(1f, control.update(0, true, 400), 0f);
        control.update(625, true, 500);
        control.update(625, true, 700);
        control.reset();
        assertEquals(1f, control.update(625, true, 800), 0f);
    }
}
