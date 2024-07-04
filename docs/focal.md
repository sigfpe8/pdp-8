What is FOCAL?
--------------

FOCAL is a programming language and an interactive environment developed originally for the PDP-8 and later ported to other PDP systems, especially the PDP-11. It has some similarities with BASIC, which was also developed around the same time, in the mid 1960's. Wikipedia has a nice [article](https://en.wikipedia.org/wiki/FOCAL_(programming_language)) explaining its history and main features.

Although BASIC can handle both numeric and character data (strings), FOCAL is more oriented to numeric calculations. You can think of it as a kind of programmable calculator on steroids. You can type in mathematical expressions that are immediatly evaluated (the *direct mode*) but you can also type in and edit programs to implement sophisticaded algorithms (the *indirect mode*).

It is pretty amazing what those guys were able to achieve with such a spartan machine as the PDP-8! In just 4 Kwords they packed the language interpreter, floating-point math functions (all in software), drivers for the keyboard, printer and papertape, and still leave room for your program and data! Absolutely sensational! 

Using FOCAL with the PDP-8 simulator
------------------------------------

On a real PDP-8 you would have your FOCAL binary on a paper tape. In order to load it you would first need to use the front panel keys to manually deposit the instructions in memory for a short program called the `RIM loader`. This program cannot load the FOCAL tape yet because it is in a different format. So you'd use the `RIM loader` to load another program called the `BIN loader`. Once this was done you would run it in order to load the FOCAL tape.

The virtual console of the PDP-8 simulator can load a binary tape directly, without the need of either of the above loaders. The contents of many DEC's original paper tapes have been converted to modern files and are available at several internet sites. PDP-8 binary files have the extensions `.bin`, `.bn` or `-bn`.

Assuming you have a FOCAL binary in your `images` sub-directory, you can load it directly with this command:
```
PC=00000> load images/focal.bin
Read 3968 locations
Addresses: 0000-7577
```

The program starts at address 0200 (octal), so you need to type
```
PC=00000> run 200
```

Depending on your version of FOCAL, you can immediatly enter the command prompt (indicated by a single `*`) or you can go first through a dialog that will ask whether you want to retain or release certain groups of mathematical functions. If you retain these functions you can use them in your programs but they occupy many memory locations. On the other hand, if you don't retain them you get more space for your data and program.

This dialog goes like this:

```
PC=00000> run 200

CONGRATULATIONS!!
YOU HAVE SUCCESSFULLY LOADED 'FOCAL,1969' ON A PDP-8 COMPUTER.


SHALL I RETAIN LOG, EXP, ATN ?:NO

SHALL I RETAIN SINE, COSINE ?:NO

PROCEED.

*
```

After you get to the `*` prompt you can type a command to check your FOCAL version.
```
*W
C-FOCAL,1969
*
```

`W` is the abbreviation of the `WRITE` command, which is used to print all the lines in your current program. Since you don't have any program yet it just displayed the version. Note that `C` is the abbreviation of the `COMMENT` command which indicates that that line should be ignored by the FOCAL interpreter. Indeed, if the first line of a FOCAL program starts with a `C`, it is simply ignored and does not occupy space in memory. However if you want to keep your comments in the body of the program for future reference, they must be in lines with a number (more later).

Imagine that you are in 1969, in front of an ASR-33 teletype and you spent all the afternoon typing and editing your new FOCAL orbit calculator. You want to keep a copy of it before you go home. Of course, there are no disks yet, nor diskettes or pen drives, all you have is some unused paper tape. What do you do? You install the paper tape in the ASR-33 punch and type `W`, but not `<RETURN>` yet. Then you turn on the paper punch and type `<RETURN>`. FOCAL will send its version, say, "C-FOCAL,1969", and then all the lines of your program to the paper tape. The first line is a comment and will be ignored when the tape is loaded in the future, but it stays there as reminder of the version for which the program was written.



