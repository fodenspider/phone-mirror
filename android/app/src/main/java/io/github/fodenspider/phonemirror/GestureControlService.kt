package io.github.fodenspider.phonemirror

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.graphics.Path
import android.os.Handler
import android.os.Looper
import android.view.WindowManager
import android.view.accessibility.AccessibilityEvent
import java.util.concurrent.atomic.AtomicBoolean

/** No window-content retrieval, event logging, text entry or background control. */
class GestureControlService : AccessibilityService() {
    companion object { @Volatile var instance: GestureControlService? = null; private set }
    private val main = Handler(Looper.getMainLooper())
    private val busy = AtomicBoolean(false)
    override fun onServiceConnected() { instance = this }
    override fun onAccessibilityEvent(event: AccessibilityEvent?) = Unit
    override fun onInterrupt() = Unit
    override fun onDestroy() { if (instance === this) instance = null; main.removeCallbacksAndMessages(null); super.onDestroy() }

    fun submit(gesture: ControlProtocol.Gesture) {
        val grant = CaptureService.controlGrant ?: return
        if (!busy.compareAndSet(false, true)) return // never build a delayed gesture queue
        main.post {
            if (CaptureService.controlGrant !== grant || instance !== this) { busy.set(false); return@post }
            try {
                val bounds = getSystemService(WindowManager::class.java).maximumWindowMetrics.bounds
                if (kotlin.math.abs(bounds.width().toDouble() / bounds.height() - CaptureService.captureAspect) > 0.04) {
                    busy.set(false); return@post
                }
                fun x(value: Int) = bounds.left + value / 65535f * (bounds.width() - 1)
                fun y(value: Int) = bounds.top + value / 65535f * (bounds.height() - 1)
                val path = Path().apply {
                    moveTo(x(gesture.x0), y(gesture.y0))
                    if (gesture.action == 2) lineTo(x(gesture.x1), y(gesture.y1))
                }
                val description = GestureDescription.Builder().addStroke(
                    GestureDescription.StrokeDescription(path, 0, gesture.durationMs.toLong())).build()
                val callback = object : GestureResultCallback() {
                    override fun onCompleted(gestureDescription: GestureDescription?) { busy.set(false) }
                    override fun onCancelled(gestureDescription: GestureDescription?) { busy.set(false) }
                }
                if (!dispatchGesture(description, callback, main)) busy.set(false)
            } catch (_: Exception) { busy.set(false) }
        }
    }
}
