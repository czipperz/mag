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
  - Buffer responses
  - While typing in normal buffers
    + file names
    + identifiers
    + tags

* Git integration
  - Git ls files

* File listeners
  - Automatically update files when file system is updated

* Screen based movement
  - Move cursor to top and bottom of screen

* Write file to path
* Comment region
* Uncomment line and region
* Run compile
* Format man pages
* Edit server
* Xorg copy paste

* Colors
  - Have some way to make faces for overlays and have the colors registered

* Multi cursors
  - When creating a cursor, try to fit it on the screen
  - Cycle selected cursor

* Eliminate copy leak bug
  - Deallocate other cursors' copies

# Bugs
* Remove all keys when sequence doesn't match instead of just the first
* Adjust window cache position by changes
  - I did a minimal version of this and it bugs out now in that the screen won't scroll up when text is insert at bob.
