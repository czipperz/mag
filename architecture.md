# Architecture

Mag runs the following infinite loop:

```
loop {
    process user events
    render
    run jobs
}
```

This loop is located in the client file (ex. `clients/sdl.cpp` and `clients/ncurses.cpp`).

## Process user events

When the user presses a key it is added to a queue of keys.  When enough elements
are in the queue to trigger a `Command`, that `Command` will be invoked.

## Rendering features

Rendering (`render/render.cpp`) does all of the following things:

* Layout windows
  - The normal windows form a tree structure.
    + All leaves in the `Window` tree are `Window_Unified` objects representing a specific buffer.
    + All non-leaves are `Window_Split` objects that point to 2 tree subtrees and
      specify the direction of the split and the ratio between the two windows.
  - The mini buffer window and the prompt pseudo-window
    are sometimes display at the bottom the screen.
    + If a command is prompting for input then both will be displayed.
    + If a message is shown then the prompt will be shown but not the mini buffer.
  - Normal windows have a title bar at the bottom.
    + Each slot after the file name is created by an `Overlay`.

* Animate scrolling

* Put characters into the grid
  - The renderer makes a grid of characters
  - The client (see `clients` folder) then renders the
    characters that have changed (either in value or in face).

* Find face for characters
  - A `Face` (`src/face.hpp`) is a combination of a foreground color,
    background color, and text modifiers (italics, bold, etc.)
  - The color a character is rendered in is that of
    the first `Face` to be applied that specifies a color.
  - Faces are applied in the following order:
    + cursor face
    + marked region face
    + `Overlay`s
      * An `Overlay` allows for adding custom syntax highlighting
      * By default `overlay_matching_region` will highlight all occurrences of
        matching regions if we're showing marks and `overlay_matching_tokens`
        will highlight all occurrencs of matching tokens if we're not.
    + `Token` faces
      * Syntax highlighting for specific languages is implemented as a lexer for that
        language.  The lexer is implemented as a state machine with a 64-bit state.
      * Syntax highlighting is generally done in parallel in the job thread.

## Run jobs

### Asynchronous jobs

A secondary thread called the job thread runs `Asynchronous_Job`s.  Asynchronous jobs
are not allowed to access any state of the editor except for using a specific
`Buffer` if and only if they hold the corresponding `Buffer_Handle`'s lock.

Jobs should run for a short amount of time (say 5-10ms) so that
* Exiting the editor while a job is running doesn't stall.
* Multiple jobs can be ran at the same time.

Jobs are typically used either to:
* Run I/O based tasks.  For example, `run_console_command` and `Run_Command_For_Completion_Results`.
* Run CPU intensive tasks.  For example, syntax highlighting large buffers.

At startup, the files specified on the command line are loaded
in an asynchronous job while the graphics system is starting.

When a file is opened or an edit to a file causes the previous syntax highlighting to be
invalidated, a job is started to syntax highlight the buffer in the background automatically.

### Synchronous jobs

Synchronous jobs run on the main thread after rendering.

The main advantage of synchronous jobs over asynchronous jobs is
that they are allowed to access the full state of the editor.

The main disadvantage is that they are less efficient (because they don't run
in parallel) and can stall rendering if run for too long in a single frame.

Asynchronous jobs can create synchronous jobs at any time to be able to access the editor's state.
This is typically done when the asynchronous job has finished the I/O interaction or CPU intensive
work and wants to "commit" its results.  Note that the state of the editor or of any buffer may
have changed inbetween the synchronous job is pushed to the queue and the time it is ran.
