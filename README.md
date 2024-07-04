# The PDP-8

This is an old unfinished project of mine that had been dormant for a long time. Looking at it recently I decided to revive it and try to improve its functionality. My initial goal was, and still is, to be able to run the software FOCAL ("FOrmula CALculator") and perhaps, being a bit more ambitious, OS/8, the disk operating system that ran on the PDP-8. OS/8 is not supported yet, but FOCAL runs fine.  Instructions for playing with it and exploring some of its capabilities are [here](docs/focal.md).


## How to build and run the simulator

To build it:

```
% mkdir build
% make
clang -std=c99 -pedantic-errors -Wall -Wextra -g -c src/console.c -o build/console.o
clang -std=c99 -pedantic-errors -Wall -Wextra -g -c src/log.c -o build/log.o
clang -std=c99 -pedantic-errors -Wall -Wextra -g -c src/main.c -o build/main.o
clang -std=c99 -pedantic-errors -Wall -Wextra -g -c src/pdp8cpu.c -o build/pdp8cpu.o
clang -std=c99 -pedantic-errors -Wall -Wextra -g -c src/pdp8asm.c -o build/pdp8asm.o
clang -std=c99 -pedantic-errors -Wall -Wextra -g -c src/tty.c -o build/tty.o
clang build/console.o build/log.o build/main.o build/pdp8cpu.o build/pdp8asm.o build/tty.o -o pdp8
```

Then, to run it:

```
% ./pdp8

PDP-8 simulator version 0.2
4K memory

Virtual console

PC=00000> 
```

Type `?` to see the available commands:

```
PC=00000> ?

PDP-8 virtual console commands:

  Command     Arguments                Purpose
  ----------  ----------------------   ----------------------
  bc          <bp #>                   Clear breakpoint
  bl                                   List breakpoints
  bp          <addr>                   Set breakpoint
  continue                             Continue
  deposit     <addr>                   Deposit memory
  examine     <addr> [<count>]         Examine memory
  help                                 Display help
  load        <file>                   Load file
  log         0|1                      Start/stop logging
  quit                                 Quit simulator
  run         <addr>                   Run program
  sacc        <value>                  Set ACC=value
  shregs                               Show registers
  si                                   Single step
  slink       0|1                      Set L=0|1
  sswt        <value>                  Set SR=value
  trace       0|1 [<file>]             Start/stop tracing
  ?                                    Display help
```

The `load` command is able to load files in a few different formats, including binary and text. 
For now we can load a simple "Hello, world!" test program written in a restricted version of the PDP-8 MACRO assembler:

```
PC=00000> load tests/hello.asm8

```

The program starts at address (octal) `200` so type

```
PC=00000> run 200
Hello, world!


HALT @ 00203  L=0  AC=0000

PC=00204> 
```

The simulator has only been tested on macOS but should probably run without problems on any Unix/Linux system. Porting to Windows should require some work because of the I/O functions.

