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
  - Select a response
  - Buffer responses

* Git integration
  - Git ls files

* File listeners
  - Automatically update files when file system is updated

* Screen based movement
  - Up and down page
  - Move cursor to top and bottom of screen

* Write file to path
* Paste previous copies
* Comment region

* Syntax C++
  - Highlight doc comments and code inside them
  - Preprocessor `#` and `##` recognition

* Multi cursors
  - Message when no cursor created
  - Make all cursors

* Jobs (run in background)
  - Syntax highlighting
  - Process for clang format
    + `fcntl(fileno, F_SETFL, O_NONBLOCK);`
    + then `read()` will return `-1`, `errno == EAGAIN`

* Eliminate copy leak bug
  - Deallocate other cursors' copies

# Bugs
* Remove all keys when sequence doesn't match instead of just the first
* Adjust window cache position by changes
  - I did a minimal version of this and it bugs out now in that the screen won't scroll up when text is insert at bob.
