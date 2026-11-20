# QAROS panel input findings

Observed on 2026-11-20 with QAROS `0.10.0-rc.4.16+793871a8`, Slicer
5.12.4 / Qt 5.15.2 and the matching public C SDK at `d90dd6a3`.
The original black-panel failure and the live input failures are separate.

## Retained stream binding: fixed in r8.12

After interrupting an owned Slicer test and reopening the same panel
identities, the 3D volume was decoded while four panels were black.
`get_or_create` had retained their previous stream IDs. Explicitly binding
current streams and restoring visible state recovered actual Qt and slice
images without deleting the panels. The calls run only at stream startup.

## Restricted panel gestures: addressed in r8.13

With the panel narrowed to the selected target peer, neither its native
callback nor Visualizer GUI-panel telemetry received live gestures.
The contact ring and decoded image alone did not establish input delivery.

The unmodified official qaros-qt example at `14c18c00` was then built and
run independently of Slicer. A real simulated-hand tap reached its native
callback and changed its label to `clicked 1 times`. That established a
working Qt/QAROS control case on this installation.

Controlled changes to the Slicer panel showed:

- An unrestricted panel immediately received hover, tap and drag events.
- Adding only the source Slicer peer to the restricted list did not restore
  sustained input.
- Including the selected receiver's internal mixer restored native events
  while the panel remained restricted.
- CPU video ingress, as used by the official example, alone did not restore
  restricted input. The experiment was removed; video still uses OpenGL.

r8.13 builds the exact panel audience from the selected targets, their live
internal mixers and the non-display source observer. Input is separately
accepted only from the selected target peers. Peer changes use the existing
SDK subscription inbox; no MCP polling is added to the plugin.

The current public peer API supplies display labels and roles, without a
mixer/target relationship in the fields used here. Resolution uses QAROS's
documented `<target> mixer` label and the internal role, with presence and
ambiguity checks. A partial selection of same-named targets does not add
ambiguous mixers: select the whole named group or give receivers distinct
names. This preserves selection rather than broadcasting controls to all
headsets. A mixer replacement triggers resolution again.

A retained audience is reconciled once, before pixels are submitted.
Only panel IDs and visible-to lists are read. Obsolete target/mixer IDs are
removed after adding the current nonempty scope, so removal never passes
through the SDK's empty-list broadcast state.

These observations identify the scope combination needed on this runtime;
they do not establish the internal implementation of its filtering.

## Drag press position: fixed in r8.13

Real SDK records showed a drag's `started` point already 23–25 pixels away
from its `start_position`. The previous adapter discarded the origin.
Qt therefore pressed the track outside the 28-pixel thumb and performed
small page steps instead of grabbing it. This matched the user's report.

The adapter now queues a press at the original contact, followed by a move
to the current point. It also reconstructs an initial update whose started
phase was missed, and clears that state on cancellation epochs or queue
overflow. This uses the existing bounded input queue and preserves ABI 5.
The thumb is wider for easier finger targeting.

The Qt regression now starts outside the thumb and verifies all three
sliders, a missing initial phase, tap ID 0, interleaved hover, and hover after
release. It asserts that release clears `isSliderDown()` and later hover
cannot continue moving the slider. All 23 main CTest checks pass. In the
live test the user confirmed movement and release now work.

## Paused streams and PythonQt callbacks: fixed in r8.13

SDK status 602 (`MediaStreamingStreamIsPaused`) was reproduced during a
receiver pause. Treating it as fatal closed all panel senders. It is now a
recoverable frame wait; the live panels survived a pause longer than 30 seconds
and resumed with their existing identities.

After dragging and release worked, the user reported that Stop removed the
controls panel. Its status showed an access violation reading address `0x60`.
The Qt bridge used `ctypes.CDLL`, which releases the Python GIL while native
mouse dispatch synchronously invokes PythonQt slots. `ctypes.PyDLL` retains
the GIL for these GUI calls.

An isolated Slicer test loads the actual Qt bridge against an in-memory host
fixture, adopts the real playback widget and dispatches native mouse taps.
With CDLL, Stop reproduced the same `0x60` access violation. With PyDLL, ten
Stop taps reached PythonQt, stopped playback and reset both the MRML browser
and its actual proxy-volume data to frame zero. No QAROS connection or user
data is used by this regression.

## Automatic-pause recovery: still open

After the Stop fix, the Qt panel remained registered and its Python/native
status contained no error, but its content became black. All four panel
senders reported `No active transfers`; the receiver reported
`Paused: no visible display` with `automatically_paused=true`.
Returning its pause policy to `never` resumed the main rendering route but
did not restore the panel receivers. Hiding/showing the Qt panel and rejoining
the same receiver also did not restore them. Restarting the owned sample
Slicer with new video senders recovered actual Qt and slice pixels.

Closing the auxiliary GUI-panel viewer and closing a separate hidden website
panel were then checked individually while the policy remained `never`;
the video panels continued playing. Restoring `windowMinimized` while the
Visualizer window was hidden reproduced automatic pausing. After returning
to `never`, all four panel streams again lost active transfers. This is a
separate recovery problem from fatal SDK status 602 and the PythonQt Stop
access violation. These observations do not establish the runtime's internal
cause. Keep automatic pausing disabled during the current Visualizer test;
do not describe general pause/resume recovery as fully validated.

## Remaining live check

The r8.15 guard reads the render sender's latest SDK hand stream only while a
drag remains outstanding. The existing panel worker runs this independently of
3D frame arrival. Each tracked fingertip is transformed back to room space using
the volume's current app/room poses and app scale, then compared with the panel's
subscribed pose. The first timestamp only primes the guard; later advancing
timestamps can cancel a withdrawn contact. The release and re-entry margins
include the fingertip radius. Old updates remain blocked until fresh re-entry,
which presses at the current contact. Every peer/hand retains its own state.
Cached/untracked data is ignored. This passes the real-Slicer fixture without
an SDK drag end, including another grab without leaving the panel; live and
physical-headset confirmation remain necessary.

The r8.14 follow-up addresses motion backlog shared by the slice and animation
controls. Continuous queued positions are coalesced for the same peer, hand,
kind and gesture; press/release/cancel events stay ordered. The real MRML
timeline test previously replayed 60 obsolete frame changes from a queued burst.
It now loads at most the final frame: its native slider has Qt tracking disabled
and commits on release. Slice controls retain their live preview. The actual
timeline hit area is increased from 15 to at least 48 logical pixels.

During live sampling a drag continued to report updated positions while the
simulated fingertip alternated between roughly z=-0.815 m (pressed) and
z=-0.745 m (withdrawn), with the panel at z=-0.8 m. Its eventual cancellation
followed leaving the panel. This may include runtime lifecycle/delivery delay;
the host's reproduced queue cost does not establish the only cause. Direct
mouse mode with the simulated hand off did not provide usable drag input for
this restricted native video panel, so simulated hand mode was restored.

Yellow/green live operation and physical headset input still need verification.
The user previously reported intermittent simulated-hand release, then
confirmed that movement and release work in the latest live test. Native status
shows the last raw SDK drag phase, the number of pressed Qt sliders,
Qt delivery/drop counts and the content dimensions, so SDK lifecycle issues
can be distinguished from widget handling.

Synthetic tests and the working standalone button do not establish that
all headset controls work. Keep the integration on its test branch until
real headset validation succeeds.
