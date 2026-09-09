TDM public V5 API header:
https://github.com/ersh1/TrueDirectionalMovement/blob/ed6b033cf07febf47e0dd563f44f7b5416f934e1/src/TrueDirectionalMovementAPI.h

Copied under the upstream permission in the header. Loader calls use REX::W32
for CommonLibSSE-NG. V5 provides temporary target-lock disable ownership.

SSC queues startup intent in its scene procedure and calls the public API from
the actual camera update. SKSE AddTask is not assumed to select the main thread.
API thread checks apply before reading target state or changing ownership;
off-thread cleanup stays pending. No TDM internals or binary offsets are patched.

In the September 10, 2026 test with Skyrim 1.6.1170, SKSE 2.2.6 and TDM 2.2.7,
TDM's main update, SSC's camera update and the API-reported ID agreed, while
scene-event tasks ran on other threads. GetTDMThreadId stores API construction
thread identity; the comparison checks the chosen callback path, not a universal
guarantee for every TDM build. A mismatch is reported rather than bypassed.
