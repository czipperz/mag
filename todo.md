# Features
* Prose
  - find file
  - search
  - finish alternate

* Directories
  - Move/rename files
  - Make directory
  - Make it not look like shit

* Read only mode

* Autocomplete
  - While typing in normal buffers
    + file names
    + identifiers
    + tags

* Screen based movement
  - Move cursor to top and bottom of screen

* Write file to path
* Uncomment line and region
* Run compile
* Edit server

* Multi cursors
  - When creating a cursor, try to fit it on the screen
  - Cycle selected cursor

* Eliminate copy leak bug
  - Deallocate other cursors' copies

* Programmable mouse events
  - left click
  - drag
  - right click
  - scroll

# Bugs
* Remove all keys when sequence doesn't match instead of just the first
* Screen jumps when cursor is at the bottom and inserting text
