/// Mag Tutorial 4
/// ==============
///
/// In the previous tutorial we saw this code example.  When we tried to
/// rename `hist` to `history` it didn't go as planned because `buddhist`
/// got changed to `buddhistory` and `history` got changed to `historyory`.
///
/// Now we'll learn about token movement commands.  Put your
/// cursor onto the declaration of `hist` in the example, and
/// then press `A-q` (forward token) then `A-j` (backward token).
/// This will go to other usages of the `hist` token.
///
/// If you use `C-q` and `C-j` then instead of moving the
/// cursor, you will create a cursor at the matching token.
///
/// Now rename `hist` to `history`.
/// Again, you can use `C-g` to exit multi cursor mode.

int hist = random();
if (hist > 0)
    hist = sqrt(3.0f) * log2(hist);
printf("the buddhist man has history value: %d", hist);



/// This might seem extremely simple, and in part that is true.
/// But this is by far the feature I use the most to navigate and edit code!
///
/// Go to the next tutorial via:
/// C-o 5-windows.mag_tutorial ENTER
