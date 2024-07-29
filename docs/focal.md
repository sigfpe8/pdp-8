What is FOCAL?
--------------

FOCAL is a programming language and an interactive environment developed originally for the PDP-8 and later ported to other PDP systems, especially the PDP-11. It has some similarities with BASIC, which was also developed around the same time, in the mid to late 1960's. Wikipedia has a nice [article](https://en.wikipedia.org/wiki/FOCAL_(programming_language)) explaining its history and main features.

Although BASIC can handle both numeric and character data (strings), FOCAL is more oriented to numeric calculations. You can think of it as a kind of programmable calculator on steroids. You can type in mathematical expressions that are immediatly evaluated (the *direct mode*) but you can also type in and edit programs to implement sophisticaded algorithms (the *indirect mode*).

It is pretty amazing what those guys were able to achieve with such a spartan machine as the PDP-8! In just 4 Kwords they packed the language interpreter, floating-point math functions (all in software), program line editor, drivers for the keyboard, printer and papertape, and still leave room for your program and data! Absolutely sensational! 

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



Playing Lunar Lander
--------------------

You can find the original *Lunar Lander* program as well as many other games written in FOCAL at this [ftp site](https://www.pdp8online.com/ftp/software/games/focal/). The typical file extension for FOCAL is `.fc`, but sometimes `.fcl` is used. This alternative extension (`.fcl`) indicates that the file contains an initial comment line with the version of the FOCAL system where the program was created. Sometimes a line with the `W` or `WRITE ALL` command is also present. The comment line with the version can be left in the file but the line with the `WRITE` command, if present, should be removed before attempting to import that file.

Importing FOCAL programs from paper tapes
-----------------------------------------

It was possible to "play back" a paper tape so that as the tape reader was reading it in it would send the characters to the computer, therefore mimicking the process of typing the program at the keyboard. However, this was probably too fast for FOCAL to digest and could result in lost characters. So FOCAL includes a mechanism where it can read in a paper tape at its own speed, avoiding errors and truncated programs.

The trick is to type a `*` at the prompt to indicate that input should continue from the Teletype paper tape (a.k.a "slow paper reader"). But before doing that we need to tell the simulator where to find the program we want to load. In other words, we need to associate the "slow paper tape reader" virtual device (device #1) with a file. This is done with the `ASSIGN` virtual console command:

```
PC=00000> assign 1 files/lunar.fc
```

Here's a full sequence of commands:

```
% ./pdp8

PDP-8 simulator version 0.2
4K memory

Virtual console

PC=00000> load images/focal.bin
Read 3968 locations
Addresses: 0000-7577

PC=00000> assign 1 files/lunar.fc

PC=00000> run 200

CONGRATULATIONS!!
YOU HAVE SUCCESSFULLY LOADED 'FOCAL,1969' ON A PDP-8 COMPUTER.


SHALL I RETAIN LOG, EXP, ATN ?:NO

SHALL I RETAIN SINE, COSINE ?:NO

PROCEED.
```

Type `*<RETURN>` here. The file `lunar.fc` will be imported and one asterisk will be printed for each input line.

```
**
*********************************************************************************************
```

Type another `<RETURN>` and then `W<RETURN>`

```
*W
C-FOCAL,1969

01.04 T "CONTROL CALLING LUNAR MODULE.MANUAL CONTROL IS NECESSARY"!
01.06 T "YOU MAY RESET FUEL RATE K EACH 10 SECS TO 0 OR ANY VALUE"!
01.08 T "BETWEEN 8&200 LBS/SEC.YOU'VE 16000 LBS FUEL.ESTIMATED"!
01.11 T "FREE FALL IMPACT TIME-120 SECS.CAPSULE WEIGHT-32500 LBS"!
01.20 T "FIRST RADAR CHECK COMING UP"!!!
01.30 T "COMMENCE LANDING PROCEDURE"!"TIME,SECS   ALTITUDE,"
01.40 T "MILES+FEET   VELOCITY,MPH   FUEL,LBS   FUEL RATE"!

02.05 S L=0;S A=120;S V=1;S M=33000;S N=16500;S G=.001;S Z=1.8
02.10 T "    ",%3,L,"       ",FITR(A),"  ",%4,5280*(A-FITR(A))
02.20 T %6.02,"       ",3600*V,"    ",%6.01,M-N,"      K=";A K;S T=10
02.70 T %7.02;I (K)2.72;I (200-K)2.72;I (K-8)2.71,3.1,3.1
02.71 I (K-0)2.72,3.1,2.72
02.72 T "NOT POSSIBLE";F X=1,51;T "."
02.73 T "K=";A K;G 2.7

03.10 I ((M-N)-.001)4.1;I (T-.001)2.1;S S=T
03.40 I ((N+S*K)-M)3.5,3.5;S S=(M-N)/K
03.50 D 9;I (I)7.1,7.1;I (V)3.8,3.8;I (J)8.1
03.80 D 6;G 3.1

04.10 T "FUEL OUT AT",L," SECS"!
04.40 S S=(-V+FSQT(V*V+2*A*G))/G;S V=V+G*S;S L=L+S

05.10 T "ON THE MOON AT",L," SECS"!;S W=3600*V
05.20 T "IMPACT VELOCITY OF",W,"M.P.H."!,"FUEL LEFT:"
05.30 T M-N," LBS."!;I (-W+1)5.5,5.5
05.40 T "PERFECT LANDING !-(LUCKY)"!;G 5.9
05.50 I (-W+10)5.6,5.6;T "GOOD LANDING-(COULD BE BETTER)"!;G 5.90
05.60 I (-W+25)5.7,5.7;T "CONGRATULATIONS ON A POOR LANDING"!;G 5.9
05.70 I (-W+60)5.8,5.8;T "CRAFT DAMAGE.GOOD LUCK"!;G 5.9
05.80 T "SORRY,BUT THERE WERE NO SURVIVORS-YOU BLEW IT!"!"IN"
05.81 T "FACT YOU BLASTED A NEW LUNAR CRATER",W*.277777,"FT.DEEP.
05.90 T "CONTROL OUT";QUIT

06.10 S L=L+S;S T=T-S;S M=M-S*K;S A=I;S V=J

07.10 I (S-.005)5.1;S S=2*A/(V+FSQT(V*V+2*A*(G-Z*K/M)))
07.30 D 9;D 6;G 7.1

08.10 S W=(1-M*G/(Z*K))/2;S S=M*V/(Z*K*(W+FSQT(W*W+V/Z)))+.05;D 9
08.30 I (I)7.1,7.1;D 6;I (-J)3.1,3.1;I (V)3.1,3.1,8.1

09.10 S Q=S*K/M;S J=V+G*S+Z*(-Q-Q^2/2-Q^3/3-Q^4/4-Q^5/5)
09.40 S I=A-G*S*S/2-V*S+Z*S*(Q/2+Q^2/6+Q^3/12+Q^4/20+Q^5/30)
*
```
