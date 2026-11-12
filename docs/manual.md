# InitialDAW manual

English | [한국어](manual.ko.md)

How to use the app, the `music-render` command line engine and the Music IR tools. The interface is in Korean: menu
items are given in English, and in Korean quotes where the English name would not find them.

- [Using the DAW](#using-the-daw): [editing](#editing), [tracks, markers and arrangement](#tracks-markers-and-arrangement),
  [audio tracks](#audio-tracks), [video](#video), [instruments and plugins](#instruments-and-plugins), [mixer](#mixer),
  [new song from a template](#new-song-from-a-template), [export](#export), [recording](#recording)
- [Music engine (music-render)](#music-engine-music-render)
- [Music IR](#music-ir)

## Using the DAW

Start a song with File > New song (⌘N) or open one with File > Open (⌘O). The track view is at the top; the bottom shows the piano roll of the selected track or
the mixer (X switches between them).
Instruments load in the background (a 17-track song takes one or two seconds), then playback is ready.

| Key or gesture | Action |
| :--- | :--- |
| Space | play and stop |
| Z X C V B N M , . / and S D G H J L ; | play the selected track from the computer keyboard: white keys from C4, black keys on the row above (no MIDI keyboard needed; chords work) |
| Q W E R T Y U I O P [ ] and 2 3 5 6 7 9 0 = | the same an octave higher (Q is C5) |
| ⌘K, ⌥↑, ⌥↓ | typing keyboard on and off (also the button in the transport bar), octave up and down. While it is on, Q and X play notes instead of quantize and mixer (both stay in the menus) |
| ⇧ Space | with notes selected in the piano roll: play all tracks from the first selected note to the end of the last, stop there and return to the start of that range (press again to stop) |
| ⌥R | record into the selected track (see Recording): stopped, it counts in and records from the playhead; playing, it records right away. Press again to finish the take; Space finishes it and stops |
| ⌥L, ⌥M | loop the song's loop range (`metadata.loop`), metronome |
| ⌥ + drag on the ruler, drag an end of the loop band | set the loop range, move its start or end (an edit, saved as `metadata.loop`) |
| Return, Home | go to start |
| ⌘N | new song from a template |
| ⌘O, ⌘R | open, reload |
| ⌘E | export to WAV, FLAC, Ogg Vorbis or MP3 |
| ⌘=, ⌘-, ⌘0 | zoom in, zoom out, fit the whole song |
| X | piano roll or mixer in the bottom pane |
| ⌘I, double-click a track header | open the instrument panel of the selected track (plugin instruments) |
| Right-click a track header | rename, duplicate, delete, move, colour, change the instrument, add a track below |
| ⌘⇧T, ⌘⇧D, ⌘⌫, ⌘⌥↑, ⌘⌥↓ | new track (pick its instrument), duplicate track, delete track, move the selected track up or down |
| Double-click a track name, drag a track header | rename the track, move it to another place |
| ⇧⌘M, ⌥←, ⌥→ | add a marker at the playhead's bar, go to the previous or next marker |
| ⌘⇧I | import a MIDI file as a new song (drop a `.mid` on the window to add its tracks to the open song) |
| ⌘⇧A, drop audio files on the track view | import audio as clips (on an audio lane: into that track; elsewhere: a new audio track per file) |
| On a clip: drag, ⌥ + drag, drag an edge, drag a top corner, ⌘T | move, copy, trim, fade, split at the playhead (right-click for gain and normalise) |
| ⌥G, then on a clip: click, drag a point, ⌥ + click a point | clip gain curve: add a point, move it, remove it |
| Up, Down | select the previous or next track (with notes selected: transpose a semitone; on an audio track, the chosen recorded note) |
| On a recorded note in the piano roll: drag, ⌥ + drag, drag a handle, drag an edge | move its centre to a semitone, move it by cents, flatten its drift or scale its vibrato, move its start or end (warp markers) |
| ⌘, | audio device settings |
| Click or drag on the ruler | move the playhead |
| Wheel, Shift + wheel, ⌘ + wheel, pinch | scroll vertically, scroll horizontally, zoom |
| ⌥ + wheel in the piano roll | key height |
| M, S on a track header | mute, solo (an edit like any other: saved in the song, undone with ⌘Z) |

### Editing

Notes are edited in the piano roll. A drag only previews; the edit is applied when you release the mouse, and one ⌘Z undoes
it. While the song is playing, the edited track switches to the new notes at the next audio block without reloading
instruments. Pressing a note or a key plays it through the track's instrument, effects and fader, stopped or playing, with
the track's transposition and the note's articulation; dragging a note plays its new pitch.

| Key or gesture | Action |
| :--- | :--- |
| Click or drag on the keyboard | play that key on the track's instrument (further right is louder), glide over the keys |
| Double-click on an empty spot | add a note (the length of the last note you clicked) |
| Right-click on an empty spot | insert a chord with its root on that key, or a scale starting there (see below) |
| Click, Shift + click | select a note, add it to or remove it from the selection |
| Drag on an empty spot | select the notes in a rectangle |
| Drag a note, ⌥ + drag | move (time on the grid, pitch by semitone), copy |
| Drag the right edge of a note | change the length |
| Double-click a note, Backspace | delete |
| ⌘A, ⌘C, ⌘X, ⌘V, ⌘D | select all, copy, cut, paste at the playhead, duplicate right after |
| Shift + Up, Shift + Down | transpose an octave |
| Left, Right | move the selection one grid step |
| Q | snap note starts to the grid (the selection, or the whole track) |
| Edit > Refine notes, or right-click a note | quantize at 25 to 100% strength, with swing, or starts and ends; widen, narrow, crescendo or diminuendo the velocities; humanize (±4 to ±14 velocity, ±6 to ±20 ms); legato or set lengths; snap to a scale; spread chords into arpeggios (up, down, up and down, one note per grid step) or strums (15 to 60 ms per note); retrograde; inversion (the selection, or the whole track; one edit each) |
| Edit > Step input (⌘⇧E) | while stopped, notes and chords played on the typing keyboard or a MIDI keyboard go in at the playhead, one grid step long, and the playhead moves one step; with no notes selected, Right is a rest, Left steps back and Backspace removes the last step |
| ⌘ while dragging | ignore the grid |
| Velocity lane | drag a bar (with several notes selected, all of them move together) |
| Controller lane | double-click to add or delete a point, drag a point, right-click for the curve shape; add a curve from the lane menu |
| Double-click the tempo | change the tempo and time signature |
| Edit > Meter from the playhead's bar | change the time signature from that bar on (2/4 to 12/8), or remove the change; the ruler shows each new meter |
| ⌘Z, ⌘⇧Z | undo, redo |
| View > Score (⌥S) | the selected track as notation in the lower pane (read only; click a bar to move the playhead there) |
| File > Export MusicXML | the song's score for MuseScore, Finale, Sibelius or Dorico |
| Edit > Undo history | the start, the past edits (oldest first, the current one ticked) and the edits you can redo; pick one to go straight there |
| ⌘S, ⌘⇧S | save, save as |

The grid is chosen under the track info on the left (quarter notes down to 32nd notes, and triplets).

**Other tracks in the piano roll.** "다른 트랙..." (other tracks) under the lane menu (also Edit > "피아노 롤의 다른 트랙" and the track
header's "피아노 롤에 함께") shows other tracks' notes in their own colours behind the track you edit, faintly ("함께 보기", show along),
or brighter and editable ("함께 고치기", edit along). With tracks edited together, a rectangle selects their notes too, and moving,
resizing, deleting, transposing, nudging and the note tools act on every track at once, as one edit (one ⌘Z). New notes,
velocities, articulations, copy and paste stay on the track you edit. Double-click a note of another track to switch to it;
the track you left keeps that track's mode. Rows in the song's key (`key`, and `keyChanges` from their beat) are lighter, the
tonic row lighter still, and the piano roll's ruler shows the markers.

**Drum view.** Tracks whose instrument names its keys (the drum kit, the built-in drum synths, the orchestral percussion)
open in the drum view: one row per named key and per key the track uses, hits drawn as diamonds sized by velocity. Click an
empty cell to add a hit there (one grid step, the last velocity), drag a hit to move it (up and down by rows), double-click a
hit to delete it. The "Drum view" button on the left switches back to the full keyboard, remembered per track.

**Chords and scales.** Right-click an empty spot in the piano roll: the key under the mouse is the root and the grid
position is the start. Edit > Insert chord and Insert scale do the same at the playhead, with the root picked from C4 to B4.
The menus hold 66 chords in nine groups (triads, sixths, sevenths, added notes, ninths, elevenths, thirteenths, altered
dominants, augmented sixths and quartal stacks) and 54 scales in nine groups (major and church modes, minor and harmonic
major, modes of melodic and harmonic minor, pentatonic scales including Korean pyeongjo and gyemyeonjo and Japanese
scales, blues, symmetric scales, bebop scales, and others such as double harmonic, Hungarian and Neapolitan). Each item
shows the chord symbol (with its Korean name) or the scale's English name, and its notes. Chords take an inversion (root position up to the third) and a length
(one grid step, a beat, two beats or a bar, by default a bar); scales go up, down or up and down over one to three octaves,
one grid step per note. The inserted notes are one edit (⌘Z), stay selected so they can be moved or transposed at once,
and the chord sounds when it lands. The tables live in the engine and are listed by `music-render chords` and
`music-render scales`.

### Tracks, markers and arrangement

The structure of a song is edited in the track view, and every change is one edit (⌘Z).

- **Tracks.** "+ Add track" under the last track, Track > New track (⌘⇧T) or Add a track below in a header's menu picks an
  instrument and inserts an empty track named after it ("Grand Piano", then "Grand Piano 2"). Duplicate (⌘⇧D) copies a track
  with its notes, curves, effects and plugin sound right below it. Double-click a track's name to rename it; compressors
  keyed by the track (sidechain) follow the new name. Drag a header to move the track; ⌘⌥↑ and ⌘⌥↓ move the selected track.
  Delete (⌘⌫) keeps at least one track, and compressors that were keyed by the deleted track become plain compressors. A
  header's Colour menu stores `"color": "#rrggbb"` on the track; without it the colour follows the track order.
- **Folders and groups.** A header's Folder menu puts the track (or every track in the selected range) into a new or an
  existing folder: a header row above the folder's tracks shows its name and size, and a click on it collapses or expands it.
  The folder's own menu renames it, mutes it, and groups it into a bus of the same name (created if missing), so the bus
  strip becomes the folder's fader and effects. Any track can also send its output to a bus instead of the master with the
  header's Output menu (`"output": "Drums"`, a subgroup); renaming or deleting the bus follows. Folders are `"folder": "Drums"`
  on the tracks and never change the sound.
- **VCA.** "+ VCA" in the mixer adds a VCA strip (name, fader, mute, member count); a track's or bus's name menu > VCA puts
  the channel in it or makes a new one. No audio passes through a VCA: its gain, and its gain curve, are added to the faders
  of its members, and its mute mutes them, while each member keeps its own fader (`"vcas": [{ "name": "Strings", "gainDb": -4 }]`
  and `"vca": "Strings"` on tracks and buses). A member track's curve lane offers "VCA gain"; renaming or deleting the VCA
  updates its members.
- **Markers.** The strip under the bar numbers holds named positions, saved as the song's `"markers": [{ "beat", "name" }]`.
  Double-click the strip to add a marker at the nearest bar and type its name, double-click a marker to rename it, drag it
  to move it, click it to go there, right-click for rename, play from here and delete. ⇧⌘M adds one at the playhead's bar,
  ⌥← and ⌥→ jump between them. `export-midi` writes them as marker events and MIDI import reads them back.
- **Sections.** The strip under the markers shows each section, from a marker to the next one (the last to the song's end),
  as a block. Drag a block and drop it at another section boundary to move the section there; click a block to go to its
  start, double-click it to select it on every track, right-click it to play from there, select it, duplicate it right
  after, move it one place earlier or later, or delete it (the rest moves up). A section takes everything with it: notes,
  audio clips (split at its edges), curves on tracks, buses and the master, tempo, meter and key changes, and markers; at its
  new place it starts with its own meter, key and tempo, and the music after it keeps theirs. Section edges must be bar lines.
- **Ranges.** Drag across the lanes to select a range of tracks and bars (whole beats when zoomed in). With a range
  selected, ⌘C, ⌘X, ⌘V, ⌘D and Backspace copy, cut, paste, duplicate right after, and clear it across those tracks. Pasting
  lands on the selected range's start or, with nothing selected, on the playhead from the selected track down, and it
  replaces what was there, so notes never pile up. Notes that start in the range move with it (a note that runs past the
  range is cut at its end); a note that starts earlier and runs into the place is cut where the place begins. Controller,
  effect and send curves are cut and pasted with their values at both ends, so the music outside the range keeps its
  values. Selecting notes in the piano roll drops the range, and selecting a range drops the notes.
- **Inserting and deleting time.** Edit > Insert time the length of the range, Delete the range's time, and Insert empty
  bars at the playhead's bar (1, 2, 4, 8 or 16) work on the whole song: every track's notes and curves, the bus and master
  curves, tempo points, markers, the loop range and `render.endBeat` move together.
- **MIDI import.** File > Import MIDI (⌘⇧I) turns a standard MIDI file (format 0 or 1) into a new song: one track per MIDI
  track and channel, instruments chosen from the General MIDI program (channel 10 plays `drum-kit`; programs without a
  matching instrument play the grand piano), the tempo map, the first time signature, markers, lyrics on their notes, and
  controller, pitch-bend and aftertouch curves as steps. Dropping a `.mid` on the window adds its tracks to the open song
  (the song keeps its own tempo).
- **Autosave.** While there are unsaved edits the app writes a copy every two minutes to its application data folder (not
  next to the song, so file watching and song generators are not disturbed). Opening a song whose copy is newer asks
  whether to restore it; restoring is one edit, so ⌘Z returns to the file's content. Saving or discarding removes the copy.

The same edits run from the command line (`music-render arrange` and `music-render import-midi`, see below), so a song
generator or an agent can reshape a song the way the app does.

### Audio tracks

An audio track plays clips of audio files (WAV, AIFF, FLAC, Ogg Vorbis, MP3) through the same chain as any other track:
effects, fader, sends, buses and the master, in playback and in export alike.

- **Importing.** File > Import audio (⌘⇧A) puts the files at the playhead's bar; dropping files on the track view puts them
  where they land (on the grid). Dropped on an audio track's lane, a file becomes a clip on that track; elsewhere, each file
  gets a new audio track named after it. Files from outside the song's folder are copied into its `audio/` folder, so the
  song folder carries everything it plays. "Audio track" in the new-track menus adds an empty one.
- **Editing clips.** Drag a clip to move it (⌥ copies it), drag its left or right edge to trim it (the start moves inside the
  file), and drag the small handles at the top corners to fade in and out. Moves and trims follow the same grid as range
  selection; hold ⌘ to place freely. Right-click a clip for gain (presets and ±1 dB), normalise (the clip's peak to -1 dBFS),
  split at the playhead (⌘T), duplicate right after (⌘D), clear the fades, show the file in Finder, and delete (Backspace).
  Every change is one edit. Waveforms are drawn on a decibel scale so quiet recordings keep their shape.
- **Reverse, fade shapes, gain curves, crossfades.** The clip menu's "Play backwards" plays the same part of the file backwards in
  the same place (`"reverse": true`; the clip's file seconds then count from the file's end, so trims, splits, warp markers
  and pitch edits keep working, and analysis such as attacks, tempo, transcription and the quick sampler reads a reversed
  copy). Each fade has a shape, drawn on the clip: S-curve (default, two opposite fades add up in amplitude), linear,
  equal power (two opposite fades add up in power) and exponential (straight in dB from -60 dB). View > Clip gain curve (⌥G)
  lets you click a clip to add gain points (top +12 dB, bottom -36 dB), drag them and ⌥-click to remove them; the curve sits
  on the file's seconds (`"gainCurve": [{ "seconds": 1, "value": 0 }, { "seconds": 3, "value": -12 }]`), so it stays with
  the sound when the clip is moved, trimmed or split, and the waveform follows it. "Crossfade with the next clip" fades a
  clip into the next one over their overlap; two clips that only touch (a split) are lengthened 10 ms into each other from
  the sound in their files. Two parts of the same continuous sound get S-curves (a split crossfaded back together renders
  within 3e-8 of the unsplit clip), different sounds equal-power fades (two unrelated noises stay within 0.08 dB through the
  overlap). Edit > Cut clips at zero crossings moves splits and trimmed edges to the nearest zero crossing within 10 ms.
  From the command line: `arrange song.json reverse Vocal --clip 2`, `fade Vocal --clip 2 --in 0.1 --out-shape exponential`,
  `crossfade Vocal [--clip 1] [--ms 20]`.
- **Timing.** A clip sits on a beat, but its audio plays in seconds: a tempo change moves where it starts, never how fast it
  plays. Range editing, inserting and deleting time split clips at the edges and move their start inside the file.
- **Recording audio.** Select an audio track and press ⌥R (the same button as MIDI recording, with the count-in). If no input
  is open, the app opens the default input's first two channels (macOS asks for microphone access the first time; pick
  another input under Settings > Audio device). The take is written as a 24-bit WAV to `audio/<track> take N.wav` while you
  play and ends when playback stops or jumps (a seek or a loop wrap). It lands as one clip, placed earlier by the output
  latency of the mix and the device's input and output latency, so it sits where you played it. A red band shows the take
  and the input level while recording.
- **Takes and comping.** Recording audio while the loop is on keeps going around the loop: the file holds every pass, and
  each pass becomes a clip on the loop (the last one plays, the earlier ones are muted takes drawn in a band under the lane).
  A clip's Takes menu lists the clips that overlap it; picking one plays it there and mutes the others, and with a range
  selected in the track view only that range switches (the clips are split at its edges), which is how a comp is built.
  A clip can also be muted from its menu (`"muted": true`).
- **Tempo detection and slicing.** A clip's menu > Detect tempo measures the tempo of the part it plays (attacks from the spectral flux, a comb over its autocorrelation, refined by how well pairs of attacks fall on whole beats; a loop cut to whole bars snaps to its length) and writes it as the clip's `sourceBpm`, so the clip follows the song's tempo. Slice at attacks (new instrument and track) cuts the clip just before each attack, maps the slices to keys from C2 in an SFZ next to the song, and adds a track that plays each slice where the clip played it, so changing the tempo or moving notes rearranges the loop. SFZ files the app writes (slices and the quick sampler) carry `volume=11.51`, which undoes sfizz's default attenuation so velocity 127 plays the recording at its own level. `music-render tempo loop.wav` and `music-render slice loop.wav --output kit.sfz [--root C2]` do the same from the command line.
- **Warp markers and audio quantize.** A clip's `warp` markers pin seconds of the file to beats of the song, and the clip stretches between them with the song's tempo map. A clip's menu > Quantize attacks to the grid (1/4, 1/8, 1/16 or their triplets, strength 100 to 25%) finds the attacks and writes markers that pull them to the grid; the clip's ends stay put. Markers show as triangles at the clip's bottom edge: drag one to move that spot of the sound (⌘ to skip the grid), ⌥-click to remove it, double-click the clip to add one. The warp mode is stretch (pitch stays) or slice (cut 10 ms before each marker and play every piece unstretched where it lands, so drums keep their attacks). On the kick, snare and hi-hat stems of a swung kit, slice puts all 61 hits exactly on a straight 1/16 grid. `music-render arrange song.json quantize-audio Drums --grid 1/16 [--strength 100] [--mode slice]` and `unwarp Drums` do the same from the command line.
- **Pitch correction.** A clip's menu > Pitch correction pulls the measured pitch of a voice or instrument to the nearest semitone or to the notes of the song's key (estimated from the notes when the song has none): fast (0 ms, flattens vibrato too), normal (30 ms) or natural (80 ms, keeps slides and vibrato), strength 100 to 25%, and keep formants. The clip's `pitchCorrection` (`strength`, `speedMs`, `key` or `scale` with `root`) is rendered with the clip, so playback and export match. On a real tenor sax recording raised 30 cents, the frames within ±10 cents of a semitone go from 1% to 93%. `music-render arrange song.json tune Vocal --key "A minor" [--speed 30]` and `untune Vocal` do the same from the command line.
- **Per-note pitch editing.** Select an audio track and the piano roll shows the notes of its recording as blobs on the pitch rows: a bar at each note's centre pitch with its measured pitch curve (notes are found as for melody transcription, once per file in the background). Drag a note up or down and its centre lands on a semitone; ⌥-drag moves it by cents. Two handles above the chosen note set how much of the slow pitch drift is flattened to the centre (0 to 100%) and how much vibrato is kept (0 to 200%); ↑ and ↓ move it a semitone, and its right-click menu resets it, snaps its centre to the nearest semitone or picks a value. Dragging a note's start or end moves warp markers placed at its edges and at its neighbours, so only the notes around it stretch. The edits are the clip's `pitchEdits` (`seconds`, `lengthSeconds` in the file, `semitones`, `flatten`, `vibrato`), rendered with the clip like pitch correction; inside an edited note the edit wins over the clip's correction. On a tenor sax take with 43 notes, moving two notes by two semitones left the other 40 within 0.7 cents. `music-render notes take.wav` lists a recording's notes, and `music-render arrange song.json pitch-edit Vocal --note 3 --to E4 [--flatten 100] [--vibrato 0]` edits one from the command line.
- **Grooves.** A groove is a timing offset and an accent per grid step over one cycle. Factory grooves (16th swing 54 to 66%, 8th swing 55 to 67%, laid back, push, backbeat accent) and your own: Note tools > Apply groove > Save a groove from these notes, or a clip's menu > Save a groove from its attacks (offsets from the 1/16 grid and levels). Apply one to notes (timing and accents together, half, timing only or accents only) or to an audio clip (Quantize attacks > To a groove, as warp markers). From a real kit with 16th swing (0.042 beats) it reads +0.033 to +0.043 beats on the offbeats. `music-render groove list`, `groove extract song.json --track Kit`, `groove extract-audio loop.wav --bpm 94` and `arrange song.json groove Hat --groove swing-16-58` do the same from the command line.
- **Follow a recording's tempo.** A clip's menu > Make the song's tempo follow this recording finds every beat of a performance played without a click (dynamic programming over the attack strength, snapped to the attacks) and writes a tempo point per beat, so the bars follow the player; the clip plays unstretched with its first beat on a bar line, and other tracks keep their beats. On a performance speeding up from 90 to 110 BPM every hit lands within 0.003 beats of the grid (0.494 before). `music-render beats take.wav` and `music-render arrange song.json follow-tempo Drums` do the same from the command line.
- **Track templates.** A track's header menu > Save as track template keeps its instrument and sound (`synth`, a plug-in's state), effects, mixer, sends, output and colour, without notes, clips or curves; an instrument defined inside the song comes along with an absolute path. Add track > From template makes a new track from a factory template (vocal, drum kit, grand piano, bass guitar, synth lead, synth pad, strings) or one of yours (`InitialDAW/TrackTemplates/`); sends to buses and sidechain keys the song does not have are left out. `music-render track-template list`, `track-template save song.json Lead --output lead.json` and `arrange song.json add-track --template vocal` do the same from the command line.
- **Melody to notes.** A clip's menu > Melody to notes (new track) transcribes the single line the clip plays (a sung or hummed take, a solo instrument) into notes on a new track under it: the pitch is tracked every 5 ms (YIN), notes are split at rests, pitch changes and re-attacks, and placed on the song's beats through the clip's position, stretch and transposition. The notes keep the timing of the performance (`"humanize": false`); quantize them in the piano roll. `music-render transcribe hum.wav --output melody.json --bpm 96` does the same into a new song.
- **Input monitoring.** Track > Input monitoring (⌥I, also in an audio track's header menu) sends the input through that
  track's effects, fader, balance and sends, playing or stopped, one track at a time. It is heard after the player's and the
  device's latency, so the interface's direct monitoring is still the low-latency choice.
- **Bouncing a track.** Track > Bounce to audio (⌘⌥B, also in the track header's menu) renders the selected track's
  instrument and effects (before the fader, with its sidechain keys playing) in the background to `audio/<track> bounce N.wav`,
  from the bar of its first sound to 0.1 s after its last. The file lands on a new audio track right after the original,
  which takes over its colour, fader, balance and sends, and the original is muted, so the mix sounds the same (within
  24-bit rounding) and can be undone in one step.
- **Stretch and pitch.** Right-click a clip for Pitch (-12 to +12 semitones, the length stays) and Stretch: follow the song's
  tempo (the clip's own tempo is set to the song's tempo at the clip, and from then on tempo changes stretch or squeeze it so
  its beats stay on the song's beats) or a fixed length of 0.5 to 2 times. In the file: `"sourceBpm": 92`, `"stretch": 1.5`,
  `"pitchSemitones": -2` (offsets and lengths stay in the file's seconds). The stretched sound is made once when the clip is
  prepared (Signalsmith Stretch), so playback, seeking, loops and export give the same samples.
- **In the song file.** The track has `"instrument": "audio"` and
  `"clips": [{ "file": "audio/take.wav", "startBeat": 16, "offsetSeconds": 0.5, "lengthSeconds": 12, "gainDb": -3, "fadeInSeconds": 0.05, "fadeOutSeconds": 1 }]`.
  Files are read once, converted to the playback sample rate and shared; playback reads a clip as a function of the song
  position, so seeking, looping and the exported file give the same samples.

### Video

File > "영상 붙이기..." (attach video; or dropping a .mp4, .mov or .m4v file on the window) puts a video against the song, to write the
music to the picture. The song keeps it as `"video": { "file": "clips/promo.mp4", "offsetSeconds": 2 }` (the song second
where the video's first frame sits; the path is relative to the song's folder when the file is inside it). The video is not
used for sound, and it stays at its seconds when the tempo changes or bars are inserted.

- **The video lane.** Above the tracks: timecode (hours:minutes:seconds:frames, drop-frame with `;` for 29.97 and 59.94 fps)
  and thumbnails, read in the background. Drag the lane to move the video (on the grid; ⌘ snaps to frames; dragging it left
  of the first bar starts the video before the song). Clicking the lane moves the playhead; right-click it for the menu below.
- **The video window.** View > "영상 창" (video window, ⌥V) shows the frame at the playhead. Stopped, it reads that exact frame; while playing
  it decodes ahead and subtracts the player's and the audio device's output latency, so the picture matches what you hear.
- **Scene changes.** "장면 바뀜 찾기" (find scene changes) in the lane's menu marks the cuts it finds (a jump in the brightness histograms of
  neighbouring frames well above the jumps around it). They are candidates, not part of the song: double-click one to put a
  marker "장면 N" (scene N) at its beat, or take them all with "장면 바뀜을 모두 마커로" (all as markers).
- **The video's sound.** "영상의 소리를 오디오 트랙으로" (the video's sound to an audio track) writes it to `audio/<video> 소리.wav` and adds an audio track "영상 소리"
  whose clip starts where the video starts (the part before the song is trimmed off).
- **Scored video.** The export dialog's "영상 (MP4)" (video) format puts the music on the video: the picture is copied without
  re-encoding, the sound is AAC (128 to 320 kbps) as long as the video, and the song's second t + offset plays at the video's
  second t. "영상의 원래 소리 섞기" mixes in the video's own sound.

### Instruments and plugins

A track whose instrument is a plugin has a panel: the plugin's own window (Dexed shows its DX7 front panel), or a list of
its parameters when the plugin has no window of its own. Open it from the instrument line under a strip's name in the
mixer, by double-clicking the track header, or with ⌘I. When you turn a knob, the app waits until the plugin has been
quiet for half a second and writes the plugin's state into the track (`"plugin": { "state": "..." }`, standard base64) as
one edit; ⌘Z puts the old sound back into the plugin. The command line renders the same sound, so the panel is part of
the song, not of the app.

- **Built-in synth panel.** Tracks that play a built-in synth (`synth.*`) open a panel drawn by the app: the preset's name, then knobs grouped into oscillator (detune width, noise), filter (on, mode, cutoff, resonance, envelope amount and decay, velocity, key tracking), amp envelope (ADSR), vibrato, pitch (pitch-bend range) and output (drive, velocity curve, stereo width, level). The filter mode (low-pass, high-pass, band-pass) is a list in the filter's header and is saved by name. Drum kits show their notes, a pitch section (tune in semitones and the pitch-bend range) and the level. Graphs show two periods of the mixed oscillators, the filter response while a note is held and when it starts, and the envelope shape; the piano and pluck presets show how long each key rings instead. The engine computes every graph with the formulas it plays with. Turning a knob is heard at once and becomes one edit when you let go; the values are stored on the track as `"synth": { "cutoffHz": 600 }`, and only the values that differ from the preset are kept. Double-click a knob for the preset value; Reset removes them all. A registry entry or an instrument defined in the song can also give a synth preset its own `"parameters"`.
- **Quick sampler.** Track > Make an instrument from audio... turns one sound file into an instrument played across the keyboard, and a clip's menu does the same with the part of the file the clip plays (spread over the keys, or one-shot for drums and effects, where every key plays the sound unchanged). The pitch is measured (YIN over the sound after its attack, the median of the clear windows), so the nearest key becomes the root and the rest is tuned in cents: a note written as E5 sounds at E5. The sound is copied into the song folder's `samples/` with an SFZ next to it, the definition goes into the song's `"instruments"` and a new track plays it, all as one edit. `music-render sampler <audio> --output <instrument.sfz> [--root C3] [--one-shot]` writes the same SFZ and reports the measured frequency.
- **Changing instruments.** Right-click a track header or a mixer strip's name: built-in synths, sample instruments, plugins from the registry, and plugins found on this computer. Choosing a found plugin also writes its definition into the song (`"instruments": [ { "name": { "type": "vst3", ... } } ]`), so the song needs no separate registry file. The old instrument's plugin settings are dropped.
- **Finding plugins.** Track > Find plugins scans VST3 (the standard folders and `external/plugins`) and AU in a separate process, so a plugin that crashes during the scan cannot take the app down. The result is the plugin cache shared with `music-render plugins scan`.
- **Which plugins load.** A plugin is code that runs as soon as it loads, so a song cannot bring its own: a VST3 named by a song or a registry loads only from the installed plugin folders (`/Library/Audio/Plug-Ins`, `~/Library/Audio/Plug-Ins`), `external/plugins`, the folders listed in `MUSIC_RENDER_PLUGIN_PATH` (separated by colons) or the plugin cache that Find plugins and `music-render plugins scan` write. Any other path is refused with a message. A plugin given directly on the command line (`plugins describe`, `plugins save-state`) is allowed.
- **Articulations.** Right-click selected notes in the piano roll to pick an articulation from the instrument's profile (`legato`, `pizzicato`, ...), or the track default. Notes show their articulation next to the pitch.
- **Band instruments.** `drum-kit` is an acoustic rock kit (MuldjordKit, CC BY 4.0: credit "MuldjordKit by Lars Muldjord") played on General MIDI keys: 36 kick, 38 snare, 42 and 46 closed and open hi-hat, 49 and 57 crashes, 51 ride, 53 ride bell, 50 48 47 45 toms, 43 41 floor tom. Velocity chooses among its recorded layers, with four takes per layer. A registry entry can remap keys like this with `"keymap": { "36": 48 }` (the key after transposition, then the instrument's key), and name them with `"keyNames"`: the piano roll then writes the names on the keys and the notes and lights only the named rows, for the kit, the built-in drum synths and the orchestral percussion. `bass-guitar` (fingered, or the `pick` articulation), `electric-guitar` (clean), `jazz-guitar` and `tenor-sax` are FreePats libraries under CC0; `engine/instruments/calibration/` has a loudness table per key for each (the saxophone and the guitars differ by 6 to 10 dB between keys).
- **Solo cello.** `cello-solo` plays the Karoryfer x bigcat cello (CC0, Copyright 2016 Karoryfer Lecolds and bigcat instruments, played by Kamila Borowiak; CC0 needs no credit, we credit it anyway) through an SFZ of our own that `tools/make-cello-sfz.py` writes next to the samples. `build.sh` runs it after `--samples` unpacks the library, and again on every build; if you keep the samples elsewhere (`MUSIC_RENDER_KARORYFER`), run the tool on that folder. Bowed notes cover keys 34 to 86 (Bb1 to D6) and pizzicato keys 34 to 75; other keys are silent. Every recording is tuned and levelled in the SFZ, including the level just after the attack, so the instrument needs no key calibration (in a chromatic run neighbouring keys differ by 0.65 to 1.3 dB on average). CC1 picks one of four recorded dynamic layers when a note starts (p up to 42, mp up to 72, mf up to 100, f above); inside the note it moves loudness (24 dB) and brightness (a shelf at 3.5 times the fundamental), and brightness never drops where two layers meet. Long notes loop where the pitch matches within 2 cents and the crossfade does not dip, with four slow loudness drifts that differ on every note. The recordings have no vibrato, so each note gets its own at 5.2 to 5.9 Hz: first notes start it 0.15 to 0.25 s in, legato notes carry it from the start, and CC21 sets the depth, also inside a note (±12 cents at the default 64, ±20 at 127, none at 0). The default articulation `legato` joins overlapping notes without a new attack and glides in from a fifth of the interval (within 30 cents after 13 ms for a whole tone and 40 ms for an octave); a repeated note gets a new bow. `sustain` takes a new bow on every note with a short release and is the one for double stops. `staccato` and `pizzicato` ring out their recordings whatever the written length.
- **Dexed.** `./build.sh --plugins` downloads Dexed 1.0.1 (GPLv3) from its GitHub release, checks its SHA-256 and unpacks it into `external/plugins/dexed`. The app and the engine load it at run time and never link or ship it. Without it, the `dexed` instrument falls back to `synth.ep`.

### Mixer

The mixer has no state of its own: faders, balance, mute, solo, sends and effects are the song's `mixer`, `sends` and
`effects`, so a mix made in the app is what the command line renders. While you drag, only the player hears the change;
when you release the mouse it becomes one edit (⌘Z undoes it). Changes apply while playing. A fader or send moves
smoothly over one audio block, and a changed effect takes over the state of the one it replaces, so reverb tails and
echoes keep ringing. Changing the tempo, the track or bus list, a sidechain key or an effect's latency on a track rebuilds
the player (a second or two for a large song).

Filter and Ducking are in the add-effect menu. A parameter that has an automation lane in the song shows (곡선, curve) after its
name and its knob is disabled; the knob shows the fixed value used when there is no lane, and editing other parameters
keeps the lanes. A send with a level lane shows 곡선 instead of its level and cannot be dragged or reset. Turning on a
delay's time in beats removes its time in milliseconds; turning it off writes the same length in milliseconds at the
first tempo.

**Automation.** The lane under the piano roll draws any curve of the selected track: Add curve offers the mixer (volume,
balance), each send, every effect parameter that can be automated, and the MIDI controllers. Each target has its own
scale (volume and sends on the fader scale, frequencies and times logarithmic) and the lane shows the value of the
dragged point or at the playhead. Double-click adds or removes a point, drag moves it, right-click picks the curve shape.
The lane box always offers Tempo (whole song): tempo points are steps or ramps, the first one stays on beat 0, and the scale fits
the song's slowest and fastest tempo. A fader or balance driven by a curve cannot be dragged in the mixer and follows the curve during playback.
View > Write automation (⌥A) records instead: while the song plays, moving a channel's fader or balance writes the move as
its curve (you hear your hand, not the old curve), and when you let go the curve returns to what it was (touch mode). Audio tracks
show the curve lane instead of velocities. Curves are the song's `automation` objects, read on a 32-sample grid of the song
position, so playback and export match; editing a curve swaps it into the playing mix without rebuilding the player.

| Gesture | Action |
| :--- | :--- |
| Drag a fader, double-click it | gain, back to 0 dB (the scale has 0 dB at three quarters of the height) |
| Drag the balance knob, double-click it | balance, back to the centre |
| M, S | mute, solo (buses have M only) |
| Click the number under a meter | clear the peak hold (red when the signal reached 0 dBFS) |
| Click a strip's name | select the track (the track view and piano roll follow) |
| Click an empty effect slot | add an effect |
| Click an effect | open its parameters on the right |
| Right-click an effect | bypass, move earlier or later, delete |
| Click an empty send slot | add a send to a bus |
| Drag a send left or right (Shift for fine steps), double-click it | send level, back to 0 dB |
| Right-click a send | pre-fader, delete |
| Double-click or right-click a bus name | rename (the sends follow), delete (its sends go too) |
| + Bus | add a bus with a reverb |

The parameter panel is built from the engine's own parameter table, so its ranges are the ones the parser accepts. An EQ
shows its frequency response, computed from the same filter coefficients the engine uses. A bypassed effect keeps its
latency, so bypassing never shifts a track. Track and bus meters show the level after the fader; the master meter shows
what you hear, after the monitor gain.

Saving keeps everything the app does not edit: comments, unknown fields, the order of keys and the extra fields of each
note. The file is written with one note per line, so a diff of an edited song is readable.

- When the file changes on disk, the app reloads it and keeps playing from the same position. If you have unsaved edits, it asks first. You can turn reloading off in the File menu.
- Running a song's generator (`compose.py`) again rewrites its JSON and replaces the edits in that file. Save hand-edited versions under another name (⌘⇧S).
- The monitor gain only affects playback. It starts at 0 dB; if the song sets a loudness target (`master.targetLufs`), the app renders the song in the background and sets the gain an export would apply, so playback is as loud as the export. Moving the slider yourself keeps your value.
- The audio device and recent files are saved in `~/Library/Application Support/InitialDAW.settings`.

The app also runs without a window, for screenshots and tests:

```bash
APP=build/app/InitialDAW_artefacts/Release/InitialDAW.app/Contents/MacOS/InitialDAW
$APP --snapshot screen.png --size 1440x900 --track 3 --at 40 --bars 9-16 --lane cc1 song.json   # draw the window to a PNG and exit
$APP --snapshot mixer.png --size 1440x900 --track 13 --at 75 --effect 13:0 song.json   # the mixer with an effect open (--effect master:0, bus1:0; --mixer alone shows no effect)
$APP --edit-test song.json              # edit a copy (notes, a curve, tempo, instruments, articulations), undo, redo, save, read it back, and drive the piano roll and the mixer with mouse events
$APP --plugin-test path/to/Synth.vst3   # open a plugin's panel without a window, move a slider, check that the state lands in the song and undo restores it
$APP --snapshot synth.png --instrument 8 song.json   # draw the built-in synth panel of track 8 to a PNG
$APP --snapshot export.png --export song.json        # draw the export dialog to a PNG
$APP --snapshot new.png --new --track 4              # draw the new song dialog with the fifth template selected
$APP --new-test [--png new.png]                      # make a song through the new song dialog without a window, open it, check it against its template (one line of JSON)
$APP --export-test [path/to/Synth.vst3] [--png done.png]   # press Export in the dialog without a window: the WAV must equal the engine's render byte for byte; also stems, MIDI, an Ogg loop, an MP3 range, stopping, and a plugin song (one line of JSON)
$APP --scan-plugins cache.json [folder] # find VST3 and AU plugins and write the plugin cache (the app runs this for Find plugins)
$APP --self-test --at 60 song.json      # open the audio device, press a note and a typing key while stopped and check they sound, play 3 s at -60 dB and check that the position moves, play a two-beat range and check it stops on its end and returns (one line of JSON)
$APP --self-test --loudness song.json   # also wait for the background loudness measurement and report the monitor gain
```

### New song from a template

File > New song (⌘N) lists the genre templates in `templates/`: empty song, piano solo, orchestral battle, boss battle,
dungeon, town dance (6/8), scherzo (3/4), orchestral theme, song form, electronic, liquid drum and bass, dubstep and
minimal electronic. A template is an ordinary song without notes: tracks with their instruments and articulations, the
mix (gain, balance, sends, effects), buses, the master (loudness target, limiter), tempo and time signature, and for game
loops a loop range (a 4-bar intro, then 32 bars). Type a title, press Create..., choose where to save, and the song opens
ready for notes. Unsaved edits in the open song are asked about first, and an existing file is never replaced.

The templates are made from the songs in this repository by `tools/make-templates.py`, so their mixes are the ones that
were measured there. Notes, tempo changes, the song's own loop and comments are removed. Controller curves become one
point per lane at the song's median value (CC1, CC11, CC21, CC74), because the track gains were set against those levels;
pedal and pitch bend curves are dropped. `tools/make-templates.py --check` reports templates that no longer match their songs.

### Export

File > Export (⌘E) writes the song as it is in the app, unsaved edits included. The dialog has:

| Setting | Choices |
| :--- | :--- |
| Format | WAV (16 or 24 bit, or 32-bit float), FLAC (16 or 24 bit), Ogg Vorbis (quality 0 to 10, default 6, about 192 kbps), MP3 (128 to 320 kbps CBR) |
| Sample rate | 44100 or 48000 Hz |
| Range | the whole song; the loop (`metadata.loop`): from the start to the loop end, with the loop points written into the file; or bars from A to B, plus a 2-second fade-out after B |
| With it | stems (one WAV per track and bus plus `master.wav`, in `<name>_stems`, whole song only), MIDI (`<name>.mid`) |
| File | the last folder you exported to (your Music folder the first time) and the song's file name, with `_loop` or `_bars17-32` for a range; Change... picks another name |

Export uses the engine's offline render, the same per-track chain as playback, so the file sounds like what you heard and
like `music-render render` on the saved song. A song with a loudness target (`master.targetLufs`) is matched to it over the
whole song, so a range is exactly as loud as it is inside the song. A progress bar runs while the song renders in the
background (Stop cancels without writing the file); when it is done, the dialog shows the length, loudness, true peak, clipping
and, for a loop, how much the end of the loop differs from the music before its start. Plugin instruments are created on
the main thread, then rendered in the background. The format, quality and folder are remembered.

- **Loop files for games.** WAV gets a `smpl` chunk (start, inclusive end); Ogg Vorbis and FLAC get `LOOPSTART` and `LOOPLENGTH` comments in samples. The last 20 ms crossfade into the audio just before the loop start, as `tools/make-loop.py` does. MP3 cannot loop sample-accurately (the encoder pads both ends): the dialog explains this and keeps Export off for an MP3 loop, and the command line refuses it.
- **Tags.** Ogg Vorbis and FLAC carry the song's `title` as `TITLE`, MP3 as an ID3v2.4 `TIT2` frame in UTF-8. MP3 files start with a LAME info frame, so players that read it remove the encoder's padding and the decoded length equals the song.
- **Same song, same bytes.** No dates are written, and the Ogg stream serial number is fixed, so exporting a song twice gives identical files.

### Recording

Press ⌥R (or the round button in the transport bar) to record into the selected track. Stopped, the app counts in (Play menu:
none, 1 or 2 bars, default 1), then plays from the playhead and records; while playing, ⌥R starts recording at once. Press
⌥R again to finish the take, or Space to finish it and stop. A take becomes one edit ("녹음", undone with ⌘Z): the notes are
added to the track with `humanize: false`, so the engine does not jitter a performance that is already human, and sustain
pedal, modulation and other controllers become points on the track's curves. The recorded notes are selected afterwards.

- **Input.** Every MIDI input device is on by default (Settings > MIDI input lists them with check marks; new devices are
  picked up within two seconds). The computer keyboard (⌘K) records as well. Whatever you play is heard through the selected
  track's instrument, effects and fader even when you are not recording, pedal and pitch bend included.
- **Timing.** Each message is stamped with the playback sample at the moment it arrives (estimated from the last audio block),
  so the latency of the user interface does not shift your notes. Play menu > "녹음할 때 격자에 맞추기" snaps note starts to
  the piano roll grid as the take is committed (lengths stay as played).
- **Metronome.** ⌥M adds a click on every beat, a higher one on the first beat of each bar, following the tempo map. It is
  mixed in after the limiter and the monitor gain, so it never reaches an export. Play menu > "메트로놈 음량" sets its level.
- **Loop and overdub.** ⌥L repeats the song's loop range (`metadata.loop`, the same range the export dialog uses). The ruler
  shows it as a band: ⌥-drag on the ruler to set a new range (snapped to beats, or to bars when zoomed out), drag an end of
  the band to move it, Edit menu > "고른 음표 구간을 반복 구간으로" to take it from the selected notes, "반복 구간 지우기" to
  remove it. Play menu > "반복 이음매 듣기" plays the last two bars of the loop into its start, repeatedly. Recording with the
  loop on keeps adding notes on every pass (overdub); "반복 구간 안에서만 녹음 (펀치)" keeps only notes that start inside the
  range. When playback wraps, the engine releases sounding notes and resends the controllers of the loop start, so what you
  hear after the wrap is what a fresh start from that point would play (checked by the engine tests).
- **Headless checks.** `--edit-test` commits a recorder take as one edit and drags loop ranges on both rulers; `--self-test`
  starts a recording on the real audio device, holds a note for 0.3 s through the MIDI input path and checks that one note
  of that length lands at the playhead.

## Music engine (music-render)

The engine is a command line tool as well. It prints one JSON object on standard output and logs on standard error, so an
agent can render, measure the result and try again without a person in the loop. Exit codes are 0 for success, 1 for
invalid input or a render failure, 2 for a usage error. The design is in [docs/plans/engine.md](plans/engine.md).

```bash
MR=build/engine/music-render

$MR validate engine/examples/piano_demo.json          # errors and warnings with JSON paths and suggestions
$MR render engine/examples/piano_demo.json --output out/piano_demo.wav
$MR render song.json --output song.wav --sample-rate 44100 --bit-depth 16 --seed 7
$MR render song.json --output song.wav --stems out/stems   # one file per track and per bus, plus master.wav
$MR render song.json --output song.mp3 --measure song.measure.json   # per bar levels, 12 bands and side per track, bus and master, and each note's head, without stem files
tools/mix-report.py song.measure.json --melody Lead --sections verse:1-8 chorus:9-16   # tables: section levels, melody against the rest above 400 Hz, band shares, low end, note levels
tools/placement-spec.py <song> [--out spec.md] [--ppq 480]   # a placement sheet for entering the song by hand: bar.beat.tick, every melody and bass note, repeated bars as patterns, drum grids, every curve point, self checks
$MR render song.json --output song.mp3                # the extension picks the format: .wav .flac .ogg .mp3
$MR render song.json --output song.ogg --quality 4    # Ogg Vorbis quality 0-10 (default 6); for MP3 it is kbps (default 320)
$MR render song.json --output loop.ogg --loop         # from the start to metadata.loop's end, loop points in the file
$MR render song.json --output part.wav --bars 17-32 --fade 2   # bars 17-32 and 2 s after them, fading out (--beats 64-128 also works)
$MR inspect out/piano_demo.wav                        # duration, peak, true peak, RMS, LUFS, clipping (and Ogg or FLAC tags)
$MR render song.json --output scored.mp4 [--video promo.mp4] [--video-offset 2] [--video-sound mix]   # the music on the song's (or that) video; the picture is copied as is
$MR video-info promo.mp4                              # length, frame rate (30000/1001 for 29.97), timecode, size, codec, sound
$MR video-frame promo.mp4 --at 12.5 --output frame.png [--width 640]   # the frame shown at that second
$MR video-scenes promo.mp4 [--song song.json]         # scene cuts (frame, timecode, and the song's beat and bar with --song)
$MR video-audio promo.mp4 --output promo.wav          # the video's sound as an audio file
$MR video-pattern --output test.mp4 --fps 29.97 --seconds 10 --scenes 2.5,5 --clicks 1   # a test video: frame numbers, scene cuts, clicks
$MR export-midi song.json --output song.mid [--humanized]   # notes, controller curves, tempo, markers, key signatures, and the notes' lyrics
$MR export-musicxml song.json --output song.musicxml [--tracks Lead,Piano]   # a score for MuseScore, Finale, Sibelius or Dorico
$MR sampler bell.wav --output samples/bell.sfz [--root C4] [--one-shot]       # an SFZ instrument from one sound (measures its pitch)
$MR transcribe hum.wav --output melody.json [--bpm 96] [--from 12 --seconds 20]   # a sung or played melody as notes
$MR tempo loop.wav [--from 0 --seconds 8]                                     # the tempo and first beat of a loop or recording
$MR slice loop.wav --output kit.sfz [--root C2]                               # one key per attack
$MR arrange song.json quantize-audio Drums --grid 1/16 --mode slice           # warp markers pull an audio track's attacks to the grid
$MR arrange song.json tune Vocal --key "A minor" --speed 30                     # pitch correction for an audio track's clips
$MR notes take.wav                                                             # the notes of a sung or played line (pitch, drift, vibrato)
$MR arrange song.json pitch-edit Vocal --note 3 --to E4 --vibrato 0             # one note of an audio clip: pitch, drift, vibrato
$MR arrange song.json reverse Vocal --clip 2                                     # play a clip backwards in the same place
$MR arrange song.json crossfade Vocal --ms 20                                    # crossfade each clip into the next (overlapping or touching)
$MR groove extract-audio loop.wav --bpm 94 --output feel.json                  # a groove from a recording (or groove extract song.json --track Kit)
$MR arrange song.json groove Hat --groove feel.json [--timing 100] [--velocity 100]   # notes (or an audio track's attacks) take the groove
$MR beats take.wav; $MR arrange song.json follow-tempo Drums                  # every beat of a free performance; the song's tempo follows it
$MR arrange song.json add-track --template vocal --name "Lead vocal"           # a track from a template (track-template list, track-template save)
$MR import-midi sketch.mid --output songs/sketch/sketch.json   # a standard MIDI file as a new song (never replaces a file without --force)
$MR arrange song.json insert-bars --at 17 --count 4  # insert four empty bars before bar 17 across the whole song
$MR arrange song.json delete-bars --bars 33-36       # delete bars and pull the rest forward
$MR arrange song.json duplicate --bars 17-24 --tracks Lead,Bass   # copy the bars right after themselves on those tracks
$MR arrange song.json copy --bars 1-4 --to 41        # or clear --bars 9-12; tracks are numbers from 1 or names
$MR arrange song.json add-track --name Pad --instrument synth.pad --at 3   # also duplicate-track, remove-track, rename-track, move-track
$MR arrange song.json add-marker --at 17 --name Chorus --output edited.json   # in place unless --output
$MR arrange song.json move-section Chorus --before Verse   # also duplicate-section, delete-section; a section is a marker's name or a number from 1
$MR format song.json [--output out.json] [--check]   # rewrite in the app's save format (one note per line); --check only reports a change
$MR instruments                                       # instrument names in the registry
$MR chords                                            # chord table: id, symbol, group, intervals from the root
$MR scales                                            # scale table: id, group, intervals
$MR templates                                         # song templates: id, name, tracks, tempo, meter, loudness target, loop
$MR new orchestral-battle --output songs/storm/storm.json --title "Storm"   # a new song from a template (never replaces a file)
$MR realtime song.json --from 60 --seconds 15         # run the real-time player at real speed and report CPU load
$MR plugins scan ~/Library/Audio/Plug-Ins/VST3        # find plugins and cache their metadata
$MR plugins describe path/to/Synth.vst3               # parameters, programs, latency
$MR plugins save-state path/to/Synth.vst3 --set Cutoff=0.4 --output synth.state
```

A song names its tracks, notes, buses and master. Beats are quarter notes, and pitches are MIDI numbers or note names
such as `E4`.

```json
{
  "tempo": 76,
  "performance": { "timingHumanizeMs": 12, "velocityHumanize": 8, "humanizeSeed": 12345 },
  "tracks": [
    { "name": "Violins", "instrument": "violins", "mixer": { "gainDb": -3, "pan": -0.4 },
      "sends": [ { "bus": "hall", "levelDb": -9 } ],
      "cc1": [ { "beat": 0, "value": 30 }, { "beat": 8, "value": 100, "curve": "smooth" } ],
      "notes": [ { "pitch": "E4", "startBeat": 0, "durationBeat": 4, "velocity": 70, "articulation": "legato" },
                 { "pitch": "B3", "startBeat": 4, "durationBeat": 0.5, "velocity": 80, "articulation": "pizzicato" } ] }
  ],
  "buses": [ { "name": "hall", "effects": [ { "type": "reverb", "mix": 1, "decaySeconds": 2.6, "preDelayMs": 25 } ] } ],
  "master": {
    "targetLufs": -16,
    "effects": [ { "type": "eq", "lowCutHz": 30 }, { "type": "compressor", "thresholdDb": -18, "ratio": 2 },
                 { "type": "limiter", "ceilingDb": -1 } ]
  }
}
```

- **Instruments.** The song only names an instrument; the registry ([engine/instruments/registry.json](../engine/instruments/registry.json), or `--registry`) decides what plays it. An entry is the built-in synth, an SFZ file, a VST3 plugin or an AU. A missing instrument falls back to the entry's `fallback`. A song can also define instruments itself: `"instruments"` takes registry file paths and objects of definitions.
- **Plugin settings per track.** `"plugin": { "state": "<base64>", "program": 3, "parameters": { "Cutoff": 0.4 } }` on a track is applied after the registry entry's own settings, so one plugin instrument can sound different on every track.
- **Synth values per track.** `"synth": { "cutoffHz": 600, "release": 1.2 }` changes a built-in synth preset (filter, envelopes, vibrato, output; piano and pluck: how long the low and high keys ring). `"filterMode": "highpass"` or `"bandpass"` turns a subtractive preset's filter into a high-pass or band-pass filter, and CC74 still sweeps it (a noise riser is `synth.noise` with `"filterMode": "highpass"` and a rising CC74 lane). `"bendRange": 12` makes `pitchBend` 1 an octave (default 2 semitones; subtractive presets, the piano and the drum kits). Drum kits take `"tune"` in semitones, and pitch bend moves their drums, including ringing ones, so a snare roll can rise. The keys and ranges are checked when the song is read; a value the preset does not use (such as `tune` on a pad) is a warning.
- **Key calibration.** Sample libraries are recorded at different levels per key, so the same velocity can jump by many dB from one key to the next (the VSCO upright piano differs by up to 14 dB between neighbouring keys). `tools/calibrate-keys.py <instrument> [--low 28] [--high 96]` renders every key at seven velocities, measures the attack level (the energy of both channels, so a recording whose channels cancel when mixed to mono is not mistaken for a quiet key) and writes `engine/instruments/calibration/<instrument>.json`. A song script can then pick, note by note, the velocity that gives the intended loudness on that key. Instruments with slow attacks whose loudness is set by CC1 (Sonatina strings and horns) are measured on the held sound instead: `--length 1.5 --window 0.5 1.3` holds each note for 1.5 seconds and measures from 0.5 to 1.3 seconds, and `--articulation legato` measures one articulation and writes `<instrument>-<articulation>.json`. For those instruments a script levels each melody note with CC11 instead of velocity. `--registry <file>` reads extra instrument definitions, so a library under test can be measured before it is added to the registry. `--cc1 <value>` holds CC1 at that value while measuring (Sonatina brass changes recording layers with CC1) and `--name <table>` names the table: a short-tongued horn line is measured with `--length 0.6 --window 0.06 0.32 --cc1 90 --name horns-short` (on short notes C5 to D5 are the loudest keys, while the held-sound table has A4 and B4 the loudest).
- **Game loops.** `music-render render song.json --output loop.ogg --loop` (or Export in the app) writes the looping game file directly and reports the seam. `tools/make-loop.py <song.json> <render.wav>` does the same from a render and also writes a listening preview and measures the seam per stem. The loop points come from `metadata.loop` (`startBeat`, `endBeat`) in the song, converted to samples with the tempo map. The loop is seamless when the bars just before the loop end are the same music as the bars just before the loop start. The tool measures how much the two places differ, per stem with `--stems`, crossfades the last 20 ms into the audio before the loop start, and writes `--output` (WAV with the loop in a `smpl` chunk), `--ogg` (Ogg Vorbis with `LOOPSTART` and `LOOPLENGTH` comments) and `--preview` (one pass, back to the loop start for `--preview-beats` more beats, faded out over `--fade` seconds).
- **Articulations.** Notes and tracks name an articulation such as `legato` or `pizzicato`. The instrument profile maps it to a keyswitch, an SFZ file, a program change or a controller.
- **Lyrics.** A note can carry the syllable sung on it: `"lyric": "늦"` (any UTF-8 text), or `"lyric": "-"` for a note that keeps singing the previous syllable. Lyrics never change the sound. `export-midi` writes each syllable as a lyric meta event (FF 05, UTF-8) at its note-on, skips `-`, and reports the count as `lyrics`. The piano roll shows the syllable inside the note (smaller on a short note, just under or above it when it still does not fit and the place is free) and `-` as a line through the note. Moving, resizing, copying and pasting a note keep its lyric; a newly drawn note has none.
- **Key.** `"key": "Bb minor"` (a tonic and major or minor, up to seven sharps or flats) and `"keyChanges": [ { "beat": 64, "key": "A major" } ]` on bar lines. They never change the sound: the score uses them for the key signature and the spelling (Db, not C#, in Bb minor; the leading tone A natural), and MIDI export writes them as key signatures (MIDI import reads them back). Without a key the score estimates one from the notes (the Krumhansl-Kessler profiles, then the lowest note at the end to choose between relative keys).
- **Score (MusicXML).** `export-musicxml` turns the song into notation: bars with meter and key changes, each staff quantized per quarter note to sixteenths (or 32nds) or triplets, chords and ties between note starts and ends, values that show the beat, accidentals per bar, a grand staff for keyboard parts that use both sides of middle C, a percussion staff for kits, tempo, markers and lyrics. Pitches are the sounding pitches.
- **Meter changes.** `"timeSignature": [4, 4]` is the first meter; `"meterChanges": [ { "beat": 64, "timeSignature": [3, 4] } ]` changes it from a bar line of the meter before (checked when the song is read). Bar numbers everywhere (the rulers, `--bars`, `arrange`, the metronome's accents, export ranges) follow the changes, MIDI export writes and MIDI import reads them, and inserting or deleting bars moves them with the music.
- **Controller curves.** `cc1`, `cc11`, `cc64`, `pitchBend` and others sit on the track as points with a `curve` (linear, step, exponential, smooth).
- **Dynamics.** VSCO strings have only two velocity layers, so when a track has a CC1 curve the engine plays every note on both layers and crossfades them with CC1. Gain and brightness follow CC1 too.
- **Effects.** `eq`, `compressor`, `reverb` (a generated hall impulse, or an `ir` file), `delay` (stereo or ping-pong; `timeMs`, or `timeBeats` to follow the tempo map, at most 16 s at the slowest tempo), `limiter` (with `"truePeak": 1` it also catches peaks between samples), `gain`, `stereo` (stage position and width), `filter`, `ducker`, `chorus`, `flanger`, `phaser`, `tremolo` and `gate`, in order on any track, bus or master. `"bypass": true` skips an effect but keeps its latency. A bus with `"mixer": { "mute": true }` is silent.
- **Filter.** `{ "type": "filter", "mode": "highpass", "slope": 24, "cutoffHz": 20, "resonance": 0 }` is a zero-latency state-variable filter: `lowpass`, `highpass`, `bandpass` or `notch`, at 12 or 24 dB per octave. `resonance` goes from 0 (a flat Butterworth response) to 1 (Q 10, a +20 dB peak). It also has `mix`.
- **MIDI learn.** View > MIDI learn (⇧⌘L) lays an overlay over the mixer: click a fader, balance knob, mute or solo button,
  a VCA, or a numeric parameter of the open effect, then move a knob or fader on your controller to bind its CC (right-click
  removes a binding). Bindings are app settings and point at channels by position ("track 1 fader"), so they work in every
  song. A bound controller moves the control as your hand would: the first value starts a drag, which previews, and 0.4 s of
  quiet lets go, which makes one edit (with Write automation on, a curve). Effect parameters follow their own scale
  (frequencies and times logarithmic), and buttons toggle when the value rises past 64.
- **Presets.** The effect editor's Presets button applies a factory preset (31 across the built-in effects, such as a vocal EQ,
  drum glue compression, plate and hall reverbs, dotted-eighth delay, sixteenth tremolo, drum gate) or one of your own, and
  saves the current values as your preset (`InitialDAW/Presets/Effects/<type>/` in the application data folder). Applying a
  preset writes its values into the song, so the song never depends on the preset, and keeps the effect's bypass, comment,
  sidechain key and any curves the preset can still use. An effect slot's "+" menu saves the channel's whole effect chain and
  loads a saved chain onto any channel (`Presets/Chains/`).
- **Modulation and gate.** `chorus` and `flanger` swing a delay between `delayMs` and `delayMs + depthMs` (with `feedback`, negative inverts it), `phaser` sweeps 2 to 12 first-order all-pass `stages` between `lowHz` and `highHz` (two stages cancel the corner frequency when mixed half and half), and `tremolo` swings the level by `depth` with a `sine`, `triangle` or rounded `square` `shape`. Their LFO runs at `rateHz`, or once every `rateBeats` beats following the tempo map, and its phase depends only on the song position, so seeking, looping and live playback match the render; `phaseDeg` makes the right channel lead (a tremolo at 180 is an auto-pan). `gate` opens when the peak passes `thresholdDb`, closes 3 dB lower after `holdMs`, and falls to `rangeDb` (`attackMs`, `releaseMs`).
- **Multiband, transient and de-esser.** `multiband` splits at `lowHz` and `highHz` (Linkwitz-Riley 4th order, the low band phase-aligned, so the bands add back flat within 0.000 dB) and compresses each band with its own `<band>ThresholdDb`, `<band>Ratio` and `<band>GainDb` (`low`, `mid`, `high`; shared `attackMs`, `releaseMs`, `kneeDb`). `transient` raises or lowers the attack (`attackDb`, the first milliseconds of a hit) and the tail (`sustainDb`) from the gaps between fast and slow envelopes of a 5 ms RMS; a steady tone passes within 0.1 dB. `deesser` reduces only the band above `frequencyHz` when its peak passes `thresholdDb`, at most `rangeDb`.
- **Ducker.** `{ "type": "ducker", "rateBeats": 1, "depthDb": -12, "attackMs": 1, "holdMs": 30, "releaseBeats": 0.5, "curve": 1, "offsetBeats": 0 }` pumps without a key track: every `rateBeats` it falls to `depthDb`, holds, and comes back over `releaseBeats` (the remaining depth is `depthDb * (1 - r)^curve`). The gain depends only on the song position, so seeking and live playback match the render exactly.
- **Automation of the fader, balance, effects and sends.** An effect object takes lanes with the same points as controller curves, for example `"automation": { "cutoffHz": [ { "beat": 64, "value": 20 }, { "beat": 96, "value": 1200 } ] }`; a send takes `"automation": { "levelDb": [...] }`; a track's, bus's or the master's mixer takes `"automation": { "gainDb": [...], "pan": [...] }`, which replace the fixed gain and balance. Lanes are allowed on `gain.gainDb`, `filter.cutoffHz`, `resonance` and `mix`, `ducker.depthDb` and `mix`, `delay.mix` and `feedback`, `reverb.mix`, `dryDb` and `wetDb`, `stereo.width`, `distortion.driveDb` and `mix`, `depthMs`, `feedback` and `mix` of `chorus` and `flanger`, `phaser.feedback` and `mix`, and `tremolo.depth`; any other parameter is an error. The first value holds before the first point and the last value after the last point. Values are read on a 32-sample grid of the song position, using the tempo map, so block size and seeking give the same samples; cutoff points are joined in log frequency. In live playback, lanes and the ducker hold the value at the playhead while playback is stopped or counting in, and after a loop wrap or a seek, sound that is still delayed in the mix keeps the values of the place it came from.
- **Plugin effects.** `{ "type": "plugin", "format": "vst3", "plugin": "/Library/Audio/Plug-Ins/VST3/Some.vst3", "state": "<base64>" }` (or `"format": "au"` with the AU identifier, `"name"` to pick one plugin of a bundle, `"program"`, `"parameters"`) runs a VST3 or AU effect in any effect slot of a track, bus or the master. The plugin hears the song position, beat and tempo through its playhead, its latency is compensated like the built-in effects, and it is prepared non-realtime for export. In the app, Add effect lists the effects found by Find plugins; a plugin slot opens the plugin's own window (or its parameter list), and a change becomes one edit that stores the plugin state in the effect.
- **Sidechain.** A compressor with `"sidechain": "Kick"` listens to the Kick track's pre-fader signal instead of its own input, on a track, a bus or the master. The key is lined up with the signal at the compressor, so latency on tracks (a reverb or limiter on a track) and effects earlier in the chain do not delay or advance the duck. The engine processes key tracks first, so offline and live playback match, and it rejects unknown keys, self keys and loops.
- **Built-in synth.** `synth.supersaw`, `synth.saw-pad`, `synth.edm-pluck`, `synth.sub`, `synth.saw-bass`, `synth.lead`, `synth.noise`, the keyboards `synth.ep` (electric piano, brighter when played harder) and `synth.poly-keys` (analog poly keys) and the `synth.edm-drums` kit (kick 36, snare 38, clap 39, hats 42 and 46, crash 49), next to the earlier piano, pad, strings, bass, pluck and drums. CC74 opens and closes the filter. `synth.club-drums` is a kit for club music whose sounds all fade out instead of stopping: 36 kick (a click and a body that falls from about 850 Hz to A1, 55 Hz at `tune` 0, gone after 0.36 s), 35 long kick, 38 snare and 40 roll snare (0.11 s, for 32nd-note rolls), 39 layered clap, 42, 44 and 46 hi-hats, 49 crash, 51 ride, 41 to 50 toms, 60 impact (a sub boom on E1 that fades out by 3.6 s), 62 and 63 downlifters (4 and 8 beats), 64, 65 and 66 noise risers (4, 8 and 16 beats), 67, 68 and 69 reverse cymbals (1, 2 and 4 beats). The effects' lengths are measured on the tempo map from where the note starts, so a 16-beat riser started 16 beats before the drop ends on the drop. Keys outside the kit are silent. `music-render instruments` lists each kit's key names. `synth.808` is an 808 bass: a sine that starts at phase 0 and falls from an octave above into the note (`pitchEnv`, `pitchEnvDecay`), decays slowly and is saturated (`drive`). It plays one note at a time (`mono`): a note that overlaps the held note glides to it in `glide` seconds without a new attack, and a note after a gap cuts the previous one. Every subtractive preset takes these four values.
- **Loudness.** `master.targetLufs` is measured before the final limiter and corrected again if the limiter takes level away.

`engine/examples/orchestral_demo.json` uses all of the above in a 64-second string arrangement.

## Music IR

A song generator knows things the JSON no longer shows: which bars are the last chorus, which track carries the melody,
which chord chart the accompaniment follows, where the main theme comes back. Next to the song JSON the generator also
writes `<song>.ir.json` with that meaning, the measured contents of every track in every section, and the statement in
`compose.py` that made each note. An agent asked to change "the strings in the last chorus" goes from the whole song to the
right lines of code in a few small queries instead of reading the generator and the JSON again, and then checks that its
edit stayed inside the bars and tracks it meant to touch. The design and the measurements are in
[docs/plans/music-ir.md](plans/music-ir.md) (in Korean).

```bash
IR=tools/music-ir.py
$IR overview <song>                       # sections, track roles, themes and where they return
$IR section <song> "<section>"               # chords, per-track measurements, themes, code lines, a command that renders only this section
$IR deps <song> --section final --track family:strings   # what the strings follow, which helpers and data other sections share
$IR source <song> --section final --track vln            # the generator statements and their current line numbers
$IR notes <song> --section p2 --track air                # the notes and controller points themselves
$IR compare <song> a1 p1                  # two sections side by side
$IR check <song>                           # is the sidecar fresh, do its references still hold (--json for any command)
$IR overview <other-song>                         # a song without a sidecar: measured values and guesses marked with a confidence
$IR regen <song>                           # run the generator in a temporary folder and list what would change
```

Edits go through a plan:

```bash
$IR plan <song> --sections final --tracks family:strings --protect role:main-melody,piano,harmony
# edit songs/<song>/compose.py
$IR verify <song>   # run the edited generator in a temporary folder and compare track by section with the plan
$IR apply <song>    # write the result; if the JSON had been edited in the app, move in only the target cells
```

- The song JSON stays byte for byte what the generator wrote before, and the renderer never reads the sidecar. `MUSICIR_OFF=1` turns the IR off.
- A generator refuses to overwrite a JSON that changed after it last wrote it (an edit saved in the app) and lists the tracks and sections that differ. `MUSICIR_FORCE=1` overwrites and keeps a backup in `build/music-ir/backup`.
- A generator with IR declares it in `describe()` in `compose.py`, calls `musicir.trace` in `Part.add` and `Part.lane`, and saves with `musicir.save` instead of `json.dump`. The tests are `tools/musicir/tests/test_music_ir.py` (CTest `music_ir`).
