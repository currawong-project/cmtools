score_gen
==========

The `cmtools` *score_gen* command parses MusicXML score files and generates a text file
which allows the score to be clarified and additional information to
be added.  This is the first step in creating a 'machine readable
score' based on the 'human readable score'.

This step is necessary because we need a way to efficiently 
append additional information to the score which cannot be entered
directly by the score editing program (e.g. Sibelius, Dorico, Finale).
In practice we use Sibelius 6 as our primary score editor.

Likewise there are certain limitations to the generated MusicXML
which need to be worked around. The primary problem being that
dynamic markings are not tied to specific notes. This is important
for purposes of score analysis as well as audio rendering. 

The overall approach to adding this addtional information
is as follows:

1. Add as much auxilliary information as possible from within Sibelius.
This entails using colored note heads, and carefully placed
text strings.

2. Generate the MusicXML file using the [Dolet 6 Sibelius
plug-in](https://www.musicxml.com/).  The resulting MusicXML file is
run through *score_gen* and parsed to find any invalid structures
such as damper up events not preceeded by damper down events, or tied
notes with no ending note. These problems are cleared by careful
re-editing of the score within Sibelius until all the problematic
structures are fixed.

3. As a side effect of step 2 a template 'edit' file is generated.
This text file has all the relavant 'machine score' information 
from the XML score as a time tagged list. In this step *edit* information is manually
added by entering codes at the end of each line.  The codes
are cryptic but they are also succinct and allow for relatively
painless editing.

4. Once the addition information is entered *xscore_gen* is
run again to generate three output files: 

- machine score as a CSV file
- MIDI file suitable for audio rendering
- SVG piano roll file

```
cmtools --score_gen -x GUTIM_20200803_utf8.xml -d edit0.txt -c score.csv -m score.mid -s score_svg.html -r report.txt
```

As with step 2 this step may need to be iterated several times
to clear syntactic errors in the edit data.

5. Generate the time line marker information to be used with the performance program:

Generate the time line marker information, into `temp/time_line_temp.txt` like this:

`cmtest -F`

This calls `cmMidiScoreFollowMain()` in app\cmMidiScoreFollow.c.

Then paste `temp\time_line_temp.txt` into kc/src/kc/data/round1.js.



Preparing the score
-------------------

Note color is used to assign notes to groups. 
These groups may later be used to indicate
certain processes which will be performed
on these notes during performance.

There are currently three defined groups
'even','dynamics' and 'tempo'.


### Score Coloring Chart:

Description         | Number   | Color
------------------- | -------- | -------------------------
Even                | #0000FF  | blue 
Tempo               | #00FF00  | green
Dyn                 | #FF0000  | red
Tempo + Even        | #00FFFF  | green + blue (turquoise)
Dyn   + Even        | #FF00FF  | red   + blue
Dyn   + Tempo       | #FF7F00  | red   + green (brown)
Tempo + Even + Dyn  | #996633  | purple

Decrement color by one (i.e. 0xFE) to indicate the last note in a group
of measured notes.

To be more precise:
Decrementing a note indicates the end of a 'measurement group'.

A measurement group is a set of notes marked for a measurement style
(e.g. even,dynamics,tempo, even+dynamics, ... ).  A measurement
section may contain multiple measurement groups - each of which is
terminated by a decremented color.

There is no concept of an 'end of section'.  The end of section is the
same as the beginning of the next section.

Note that if the terminating note of a given measurement group
contains two styles then only one color needs to be decremented to end
the group. For example if the terminating note is marked for both
evenness and dynamics (red + blue) then decrementing either red or
blue will terminate both the evenness and dynamics measurements.


### Section Numbering

Rectangles around a number indicate sections numbers.
Sections are used to aggregate measurements and to 
indicate where particular transforms will be applied in the electronic score.

TODO: Show screen shot


Preparing the Music XML File
----------------------------

*xscore_gen* is known to work with the MusicXML files produced by
the [Dolet 6 Sibelius plug-in]<https://www.musicxml.com/>

After generating the file it is necessary to do some
minor pre-processing before submitting it to *xscore_gen*

```
iconv -f UTF-16 -t UTF-8 -o score-utf8.xml score-utf16.xml
```

On the first line of score-utf8.xml change:

```
<?xml version='1.0' encoding='UTF-16' standalone='no'?>
```

to 

```
<?xml version='1.0' encoding='UTF-8' standalone='no'?>
```

Note that naively opening `score-utf8.xml` with emacs will
crash emacs because the stated XML file encoding `encoding='UTF-16'`
will not match the actual file encoding (UTF-8).  Work around
this problem by editing the first line with `vi`.



Create the edit file
--------------------------

```
cmtools --score_gen -x myscore.xml -d myedit.txt {--damper}
```

Here's a snippet of a typical 'edit' file.

```
Part:P1
  1 : div:768 beat:4 beat-type:4 (3072)
      idx voc  loc    tick  durtn rval        flags
      --- --- ----- ------- ----- ---- --- ---------------
        0   0     2       0     0  0.0     |--------------
        1   0     0       0    54  4.0     --------------- 54 bpm
        2   1     0       0  3072  1.0     -R-------------
        3   5     0       0  2304  2.0     -R-.-----------
        4   0     0     996     0  0.0     --------V------
        5   0     0    1920     0  0.0     --------^------
        6   5     0    2304   341  8.0     -R-------------
        7   0     0    2643     0  0.0     --------V------
        8   5     0    2645    85 32.0     -R-------------
        9   5     3    2730    85 32.0 F 5 --------------*
       10   5     4    2815    85 32.0 Ab2 --------------*
       11   5     5    2900    85 32.0 C 3 --------------*
       12   5     6    2985    87 32.0 F 6 --------------*
       13   1     0    3072   768  4.0     -R-------------
       14   5     0    3072   768  4.0     -R------------- 3840
```

### Edit file format

Column | Description
-------|-----------------------------
idx    | event index
voc    | voice index
tick   | MIDI tick 
durtn  | duration in MIDI ticks
rval   | rythmic value
pitch  | scientific pitch
flags  | event attributes 

### Event attribute flags:

Event attribute symbols used in the edit file:

Desc      | Flag | 
----------|------|-----------------------------------------
Bar       |  \|  | Beginning of a measure
Rest      |  R   | Rest event
Grace     |  G   | Grace note event
Dot       |  .   | note is dotted
Chord     |  C   | note is part of a chord
Even      |  e   | note is part of an 'even' group
Dyn       |  d   | note is part of a 'dynamics' group
Tempo     |  t   | note is part of a 'tempo' group
DampDn    |  V   | damper down event
DampUp    |  ^   | damper up event
DampUpDn  |  X   | damper up/down event
SostDn    |  {   | sostenuto down event
Section   |  S   | section boundary
SostUp    |  }   | sostenuto up event
Heel      |  H   | heel event 
Tie Begin |  T   | begin of a tied note
Tie End   |  _   | end of a tied note
Onset     |  *   | note onset  



Edit Sytax:
------------------

```
!<dyn_mark>             Assign dynamics
!(<dyn_mark>)             - less uncertain dynamic mark
!<upper-case-dyn-mark>    - begin of dynamic fork (See note below regarding dynamic forks)
!!<upper-case-dyn-mark>   - end of dynamic fork                      
~<mark>                 Insert or remove event (See pedal marks below.)
@<new_tick_value>       Move event to a new time position
%<grace_note_flag>      Flag note as a grace note
%%<grace_note_flag>         -last note in grace note sequence
$<sci_pitch>            Assign a note a new pitch
[ <comment> ]           Add an arbitrary comment.

<grace_note_flag>
  b (begin grace)
  a (add grace and end grace)
  s (subtract grace and end grace)
  g (grace note) 
  A (after first)
  N (soon after first)
```

Note: The first non-grace note in a grace note sequence is marked with a %b.
The last non-grace note in a grace note sequence is marked with a %s or %a.

> %s = steal time from the note marked with %b.
> %a = insert time prior to the note marked with %a.
       
 The last (by row number) note (grace or non-grace) in the the sequence
is marked with %%# where # is replaced with a,b,s,or g.

It is only necessary to mark the tick number of grace notes in order
to give the time sequence of the notes. A single grace note therefore does
not require an explict tick mark notation (i.e. @####)

Note that a given event (e.g. note,rest,bar) can be marked as both the
begin (i.e.%b) and end (e.g. %%a, %s) of a grace note sequence. 


Insert/delete  Event Marks: <mark>
-----------------------------------

Mark | Note
-----|--------------------------------------------------------------------------
 d   | sostenuto down just after note onset
 u   | sostenuto up just before this event
 x   | sostenuto up just before this event and down just after it
 D   | damper pedal down after this event
 U   | damper pedal up before this event
 _   | set tie end flag
 &   | skip this event


Dynamic Marks: <dyn-mark>
--------------------------

```
  s       (silent note)
  pppp-
  pppp
  pppp+
  ppp-
  ppp
  ppp+
  pp-
  pp
  pp+
  p-
  p
  p+
  mp-
  mp
  mp+
  mf-
  mf
  mf+
  f-
  f
  f+
  ff
  ff+
  fff
```

Note: Dynamic Forks:
--------------------

Use upper case dynamic letters to indicate forks in the dynamics
which should be filled automatically.  Note that all notes
in the voice assigned to the first note in the fork will be
included in the dynamic change. To exclude a note from the
fork assign it a lower case mark.

Common Error Messages:
----------------------

The tied C#3 in measure 13 (tick:198460) was not terminated.

The tied Bb3 in measure 12 marked as a tied note but is also marked to sound.


Damper down not preceded by damper up in measure:10.

Damper up not preceded by damper down in measure:23.

Damper up/down not preceded by damper down in measure:%34.


A shorten/shift operation was done to reconcile two overlapped D4 (ticks:1651523 1651983) notes in measure 335.

A time embedded note (bar=350 A5) was removed even though it overlapped with a note in the same voice.
