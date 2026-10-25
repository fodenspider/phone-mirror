package io.github.fodenspider.phonemirror

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.graphics.Path
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.WindowManager
import android.view.accessibility.AccessibilityEvent
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/** No window-content retrieval, event logging, text entry or background control. */
class GestureControlService : AccessibilityService() {
    companion object { @Volatile var instance: GestureControlService? = null; private set }
    private val main = Handler(Looper.getMainLooper())
    private val pending = AtomicInteger(0)
    private val releasePosted = AtomicBoolean(false)
    private var busy = false
    private val touch = TouchLease()
    private var touchGrant: Any? = null
    private var bounds = Rect()
    private var stroke: GestureDescription.StrokeDescription? = null
    private var lastX = 0f
    private var lastY = 0f
    override fun onServiceConnected() { instance = this }
    override fun onAccessibilityEvent(event: AccessibilityEvent?) = Unit
    override fun onInterrupt() { releaseTouch() }
    override fun onDestroy() {
        if (instance === this) instance = null
        touch.clear(); stroke = null; main.removeCallbacksAndMessages(null)
        super.onDestroy() // Android removes gestures owned by the disconnected service.
    }
    fun releaseTouch() {
        if (releasePosted.compareAndSet(false, true)) main.post {
            releasePosted.set(false); touch.release()
        }
    }

    fun submit(gesture: ControlProtocol.Gesture) {
        val grant = CaptureService.controlGrant ?: return
        if (pending.incrementAndGet() > 8) {
            pending.decrementAndGet(); releaseTouch(); return
        }
        main.post {
            pending.decrementAndGet()
            if (CaptureService.controlGrant !== grant || instance !== this) return@post
            val current = getSystemService(WindowManager::class.java).maximumWindowMetrics.bounds
            if (kotlin.math.abs(current.width().toDouble() / current.height() - CaptureService.captureAspect) > 0.04) {
                touch.release(); return@post
            }
            if (gesture.action >= 3) {
                if (gesture.action == 3 && busy) return@post
                if (!touch.accept(gesture.action, gesture.x0, gesture.y0, SystemClock.uptimeMillis())) return@post
                if (gesture.action == 3) {
                    bounds = current; touchGrant = grant; stroke = null
                    lastX = x(touch.x); lastY = y(touch.y)
                    advanceTouch()
                }
                return@post
            }
            if (busy || touch.active) return@post
            busy = true
            try {
                fun px(v: Int) = current.left + v / 65535f * (current.width() - 1)
                fun py(v: Int) = current.top + v / 65535f * (current.height() - 1)
                val path = Path().apply {
                    moveTo(px(gesture.x0), py(gesture.y0))
                    if (gesture.action == 2) lineTo(px(gesture.x1), py(gesture.y1))
                }
                val description = GestureDescription.Builder().addStroke(
                    GestureDescription.StrokeDescription(path, 0, gesture.durationMs.toLong())).build()
                val callback = object : GestureResultCallback() {
                    override fun onCompleted(gestureDescription: GestureDescription?) { busy = false }
                    override fun onCancelled(gestureDescription: GestureDescription?) { busy = false }
                }
                if (!dispatchGesture(description, callback, main)) busy = false
            } catch (_: Exception) { busy = false }
        }
    }
    private fun x(v: Int) = bounds.left + v / 65535f * (bounds.width() - 1)
    private fun y(v: Int) = bounds.top + v / 65535f * (bounds.height() - 1)
    private fun advanceTouch() {
        if (!touch.active) return
        val authorized = CaptureService.controlGrant === touchGrant && instance === this
        val ending = touch.mustEnd(SystemClock.uptimeMillis(), authorized)
        // On revocation only lift at the last position; never inject new motion.
        val nextX = if (authorized && !touch.cancelled) x(touch.x) else lastX
        val nextY = if (authorized && !touch.cancelled) y(touch.y) else lastY
        // A stationary continuation with willContinue=true produces no motion
        // events in Android's injector and is rejected. Keep the existing pointer
        // down, checking its lease without dispatching another gesture.
        if (stroke != null && !ending && nextX == lastX && nextY == lastY) {
            main.postDelayed({ advanceTouch() }, 16)
            return
        }
        try {
            val path = Path().apply { moveTo(lastX, lastY); if(nextX != lastX || nextY != lastY) lineTo(nextX, nextY) }
            val next = stroke?.continueStroke(path, 0, 32, !ending)
                ?: GestureDescription.StrokeDescription(path, 0, 32, !ending)
            val description = GestureDescription.Builder().addStroke(next).build()
            val callback = object : GestureResultCallback() {
                override fun onCompleted(gestureDescription: GestureDescription?) {
                    if (ending) { touch.clear(); stroke = null; touchGrant = null }
                    else { stroke = next; lastX = nextX; lastY = nextY; advanceTouch() }
                }
                override fun onCancelled(gestureDescription: GestureDescription?) {
                    touch.clear(); stroke = null; touchGrant = null
                }
            }
            if (!dispatchGesture(description, callback, main)) { touch.clear(); stroke = null; touchGrant = null }
        } catch (_: Exception) { touch.clear(); stroke = null; touchGrant = null }
    }
}
