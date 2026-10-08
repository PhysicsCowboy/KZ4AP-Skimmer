# Bell 1977 listing: transcription log

This log records how two independent readings of E. L. Bell's Fortran listing (thesis pp. 158-191) were settled into
`listing.txt`. It has one row per line where the readings differed beyond spacing, or where either reading carried a
mark. The readings themselves are working files and are not committed; each row quotes both of them.

## Method

- **The readings.** Reader A and reader B were separate agents. Each transcribed all 34 pages from images of the scan
  rendered at 170 dpi, zooming at 300 dpi and above where needed. Neither saw the other's work, and neither used the
  PDF's text layer.
- **The images.** In the scan, each page carries its text as a 1-bit JBIG2 mask of about 350 dpi, 2576-2761 px wide
  and placed exactly on the page, over a low-resolution background. The mask holds the shape of the text; the colors that
  paint it vary. For each listing page the mask was copied out of the PDF unchanged and rendered alone with
  Ghostscript at its own resolution, pixel for pixel (ink 0, paper 255). All measurements below are on these masks.
  Pages were also rendered whole at 300 dpi for overviews. Sizes are in pixels (px) of the mask: 1 px = 1/350 in.
- **The comparisons.** A line grid and a character grid were fitted on each page, including its skew. The fitted
  pitches are about 59.4 px per line and 35.7 px per column, about 6 lines and 10 characters per inch. A doubtful
  glyph was compared with the same printer's glyphs for each candidate character, on the same and neighboring pages,
  taken from lines both readers agree on. The comparisons were visual, at up to 10x, and by measurement: heights,
  widths, holes, and the position of ink within the character cell. The overlap of the binary bitmaps was also
  scored. That score is IoU, intersection over union, maximized over shifts of up to 4 px. "Mean IoU of the 5 best
  matches" is quoted per candidate.
- **The JBIG2 check.** A JBIG2 encoder stores repeated shapes once and copies them. So a glyph that the encoder
  had merged with another character would be an exact pixel copy of other glyphs on its page. No glyph settled from
  the image has a pixel copy on its page, except the main part of the digit on p. 163 at 05900, which is a copy of a
  fragment of a 0 on the same page (see its row). Apart from that one, every glyph decided from the image shows the
  scan's own shape.
- **Classes.**
  - *image*: the picture decides.
  - *context*: the picture cannot decide, and the Fortran decides: a statement label that must exist, a variable
    name used elsewhere, or the syntax. Three rows rest on the meaning of a text or a constant instead, and say so:
    p. 160 02100, p. 163 03200 and p. 180 27500.
  - *neighbors*: a sequence-number digit cut off at the scan's edge, reconstructed from the sequence numbers around
    it, written `{d}`.
  - *kept*: a mark left in the transcription.
  - *not a character*: a speck that reader A noted, measured on the image.
  - *notation*: a reader's description, not a transcribed character.

## Counts

| Item | Lines |
|---|---:|
| Lines in `listing.txt` | 1 814 |
| of which numbered listing lines (pp. 159-191) | 1 735 |
| of which page headers `=== p. N ... ===` and page-number lines | 34 + 34 |
| of which the figure page p. 158 (description and labels) and the heading "COMPUTER PROGRAMS" (p. 159) | 10 + 1 |
| Identical in both readings | 1 715 |
| Differing in spacing only (the heading on p. 159, indented by reader B) | 1 |
| **Differing beyond spacing** | **98** |
| - a character differed; settled by image | 19 |
| - a character differed; settled by context | 18 |
| - only the cut-off first digit of the sequence number differed (p. 163); reconstructed from neighbors | 52 |
| - reader A noted a speck; not a character (image) | 7 |
| - the readers described the same thing differently (p. 158 figure page; p. 168 07400 cut-off) | 2 |
| **Identical in both readings but carrying a mark** | **112** |
| - a character, settled by image (p. 161 08100, p. 164 07100, p. 166 01700) | 3 |
| - a cut-off sequence digit, reconstructed from neighbors (p. 169: 59, p. 177: 11, p. 178: 9) | 79 |
| - a cut-off sequence digit, kept as `[?]` (p. 159) | 30 |

In all: 22 characters and 7 specks settled from the image, and 18 characters settled by context. Sequence-number
digits reconstructed from neighbors: 131 lines in the sequence-number table, plus 9 lines in the character table
whose digit was reconstructed the same way, so 140 digits. Marks in the readings: reader A 196 (173 `[?]`, 23 `[?x|y]`),
reader B 123 (112 `[?]`, 11 `[?x|y]`).

**Marks left in `listing.txt`:** no `[?x|y]`; 31 `[?]`, all the first digit of a sequence number on p. 159, cut off
by the scan's left edge (probably 0, see below); 1 `[cut off at the scan's right edge]` (p. 168 07400). Reconstructed
digits `{d}`: 140, all the first digit of a sequence number (p. 163: 60, p. 169: 60, p. 177: 11, p. 178: 9).

**Irregular sequence numbers, as printed.** Within a numbering group the numbers step by 100, except here. Settled
in this log:
- p. 167: 02700, 02900, 02900, 03000 (02900 twice).
- p. 168: 06900 where 08900 would follow 08800 (06900 also numbers a line 20 lines earlier on the page).
- p. 184: 04000, 04120, 04200.

Read alike by both readers:
- p. 168: 09750 between 09700 and 09800.
- p. 182: 37500 and 37800-38000 absent.
- p. 183: 42550, and 42710-42730.

**Checked although the readers agree.** In the DATA table MORSE (p. 163, 05300), the third row, for C, is printed
2,3,1,3,1,3,1, the same as the row for B (dash-dot-dot-dot); the fifth value is a clear 1. The line is transcribed as
printed.

## Characters

Page, sequence number (as settled), both readings, the settled line, the class and the reason. In the readings, `[?x|y]`
is a reader's mark for "x or y" and a trailing `[...]` a reader's note. Line numbers such as "03400" refer to the
sequence numbers on the same page unless a page is given.

| Page | Seq. no. | Reader A | Reader B | Settled | Class | How settled |
|---|---|---|---|---|---|---|
| 158 | - | `[figure page, printed rotated 90 degrees; not part of the listing; no line numbers]` | `[figure page, printed sideways (landscape); no program text]` | `[figure page, printed rotated 90 degrees (landscape); not part of the listing: no program text, no line numbers]` | notation | Reader notes on the page, not transcribed characters; the two wordings merged. |
| 159 | [?]2900 | `[?]2900           G[?O\|U] TO 1` | `[?]2900           GO TO 1` | `[?]2900           GO TO 1` | context | Image: the second letter is a U shape, the top and upper left of the O unprinted. Context: GU TO 1 is not a Fortran statement; GO TO 1 goes to label 1 (line 1000, the main loop). The first digit of the sequence number stays [?] (see the sequence-number table). |
| 160 | 01800 | `01800    300  FORMAT(1X,'INPUT SIG PARMS[?=\|-] AVAR,BVAR,FCHIRP,TCHIRP,PHIVAR')` | `01800    300  FORMAT(1X,'INPUT SIG PARMS- AVAR,BVAR,FCHIRP,TCHIRP,PHIVAR')` | `01800    300  FORMAT(1X,'INPUT SIG PARMS- AVAR,BVAR,FCHIRP,TCHIRP,PHIVAR')` | image, 350 dpi | One bar at mid-height, 13 rows by 23 px. The minus signs on p. 163 (01200, 02300) are single bars, 12-13 rows by 23-24 px. The equals signs on this page are two bars of 7-9 rows each, spanning 21-22 rows, 19-20 px wide. |
| 160 | 02100 | `02100    400  FORMAT(1X,'INPUT S[?I\|T]G PARMS: GAMMA,FREQ,NOISE')` | `02100    400  FORMAT(1X,'INPUT SIG PARMS: GAMMA,FREQ,NOISE')` | `02100    400  FORMAT(1X,'INPUT SIG PARMS: GAMMA,FREQ,NOISE')` | context | Image: a top bar and a stem with no bottom serif, 32 px tall. Several I's on pp. 160-162 also print without their bottom serif (for example p. 161 11400, IBLANK: same height, same top), so the image does not separate I from T. Context: the same prompt phrase, 'INPUT SIG PARMS', is unambiguous three lines above (01800). This is text inside a FORMAT literal, so the reasoning is about the words, not Fortran syntax. |
| 160 | 04300 | `04300    500  RETURN` | `04300    5[?0\|4]0  RETURN` | `04300    500  RETURN` | context | Image: the middle digit shows a slash, a right side and a bottom fragment. That fits a broken slashed zero or a 4 without its crossbar. Context: the only branch here is GO TO 500 at 03400, and INPUTL has no other label 500; a label 540 would leave that GO TO without a target. |
| 161 | 08100 | `08100        2  '8','9','0',';',':','%','&',0,0,'[?X\|K]','.','AS','SN',` | `08100        2  '8','9','0',';',':','%','&',0,0,'[?X\|K]','.','AS','SN',` | `08100        2  '8','9','0',';',':','%','&',0,0,'X','.','AS','SN',` | image, 350 dpi | Two crossing diagonals with a diagonal lower-left arm and no vertical stem; the upper-left arm is unprinted. The K's on this page (07900, 10700) have a vertical stem on the left. Both readers marked it. Note: IALPH(43) = 'X' repeats IALPH(24) = 'X'. |
| 161 | 10600 | `10600    30     FORMAT(8I3)` | `10600    30     FORMAT(8I5)` | `10600    30     FORMAT(8I3)` | image, 350 dpi | The upper half is this printer's flat-topped 3: the right end of the top bar, then a diagonal down to the middle (the left part of the top bar is unprinted). A 5 has a vertical left stroke under a top bar to the right (07300, 11900). Compared at 10x. |
| 163 | {0}2300 | `[?]2300         IF(AMPB.LT.0.001) BFADE=0.021-AMP` | `02300         IF(AMPB.LT.0.001) BFADE=0.001-AMP` | `{0}2300         IF(AMPB.LT.0.001) BFADE=0.021-AMP` | image, 350 dpi | The digit after 0.0 has a top arc with a hook at the left, a diagonal and a flat base: this printer's 2 (01100, 05400, 05500). The 0s on the page have both sides of an oval. The 0.001 in the IF is read alike by both readers. |
| 163 | {0}3200 | `[?]3200         PHI=AMOD(PHI,6.28319)` | `03200         PHI=AMOD(PHI,[?6\|8].28319)` | `{0}3200         PHI=AMOD(PHI,6.28319)` | context | Image: only a closed lower loop is printed, hole 12 px high and 15 px wide, top at the height of a 6's bowl. On pp. 161-164 the bowl of a 6 (holes 11-13 px high, 15-17 px wide) and the lower loop of an 8 (11-13 px high, 14-18 px wide) are the same size, so the image does not decide. Context: the parallel statement four lines above (02800) wraps THETA with 6.28319 (2 pi), and INPUTL uses 6.28319 (03000, 03100). Numerical context, not syntax. |
| 163 | {0}3600 | `[?]3600         CALL RAN[?D\|O]N(ZN,1,0.,RSIGM)` | `03600         CALL RANDN(ZN,1,0.,RSIGM)` | `{0}3600         CALL RANDN(ZN,1,0.,RSIGM)` | image, 350 dpi | Straight left side, like the D of RANDN in 01500, 01900 and 03000; the O's on the page have rounded sides. Context agrees: SIMSG1 calls RANDN three more times. |
| 163 | {0}4600 | `[?]4600           DIMENSION IOUT(500),ISYM[?B\|R]L(6),ITEXT(200)` | `04600           DIMENSION IOUT(500),ISYMBL(6),ITEXT(200)` | `{0}4600           DIMENSION IOUT(500),ISYMBL(6),ITEXT(200)` | context | Image: a bowl and a leg with no bottom bar, which looks like R. But this printer's B often prints without its bottom bar: the B of SUBROUTINE at 04400 on this page looks like R. Context: the array is ISYMBL in the DATA statement (p. 164 07100, where the B has its bottom bar). KEY compiles only if all three statements use the same name. |
| 163 | {0}4800 | `[?]4800           COMMON/BLK1/TAU/BLK6/DMEAN,XDU[?R\|P],ESEP,EDEV` | `04800           COMMON/BLK1/TAU/BLK6/DMEAN,XDUR,ESEP,EDEV` | `{0}4800           COMMON/BLK1/TAU/BLK6/DMEAN,XDUR,ESEP,EDEV` | context | Image: a bowl and a stem with no leg (P-like, or R with an unprinted leg). Context: KEY uses XDUR (p. 164: 07400, 11700, 11800), and INPUTL's COMMON/BLK6 lists XDUR (p. 160 00300) with DATA XDUR/0./ (00700). |
| 163 | {0}5000 | `[?]5000           DATA IK/"001000000200/` | `05000           DATA IK/"001000000000/` | `{0}5000           DATA IK/"001000000000/` | image, 350 dpi | The tenth digit is a broken slashed zero: a fragment at the upper left, a straight slash, fragments of the right side, and a flat bottom fragment. It matches the zero in 1000. on p. 164 07300 closely. Mean overlap (IoU) of the 5 best matches: 0.59 against 0s, 0.40 against 2s (pp. 163-164). This printer's 2s are smooth S-curves. |
| 163 | {0}5300 | `[?]5300        2  2,3,1,3,1,3,1,0,0,0,2,3,1,3,1,3,1,[?0\|2],0,0,` | `05300        2  2,3,1,3,1,3,1,0,0,0,2,3,1,3,1,3,1,0,0,0,` | `{0}5300        2  2,3,1,3,1,3,1,0,0,0,2,3,1,3,1,3,1,0,0,0,` | image, 350 dpi | A broken slashed zero: thick top right, a straight slash and fragments, like its right-hand neighbor and the zeros at 05800 and p. 164 07400. Not this printer's S-curved 2. Mean IoU of the 5 best matches: 0.59 against 0s, 0.43 against 2s. Every row of the DATA table alternates element codes with 3 and is padded with 0, which agrees. |
| 163 | {0}5900 | `[?]5900        2  2,3,1,0,0,[?0\|1],0,0,0,0,2,3,2,3,2,0,0,0,0,0,` | `05900        2  2,3,1,0,0,0,0,0,0,0,2,3,2,3,2,0,0,0,0,0,` | `{0}5900        2  2,3,1,0,0,0,0,0,0,0,2,3,2,3,2,0,0,0,0,0,` | image, 350 dpi | The stroke lies 7 px right of the cell's center, where the zeros on this line have their right sides. A fragment sits where their upper-left arcs are. The 1s on this line have their stems at the center. The glyph's main bitmap is a pixel copy (one JBIG2 symbol) of a fragment of the last 0 of the sequence number 05500. |
| 164 | 07100 | `07100           DATA ISYMBL/1H.,1H[?_\|⊥],1H ,1H/,1H;,1H\/` | `07100           DATA ISYMBL/1H.,1H[?_\|⊥],1H ,1H/,1H;,1H\/` | `07100           DATA ISYMBL/1H.,1H_,1H ,1H/,1H;,1H\/` | image, 350 dpi | A full-width bar below the baseline (rows 709-715 px; the line's baseline is at row 706), with a central tick 11 px tall. The only printable ASCII character drawn as a bar below the baseline is the underscore. The listing has no other underscore to compare. Both readers marked it [?_\|⊥]. Settled by the image plus the character set; the tick is unexplained. |
| 164 | 08200 | `08200           IF((Y.LE..9).AND.(Y.GT..3)) IELM=6` | `08200           IF((Y.LE..9).AND.(Y.GT..8)) IELM=6` | `08200           IF((Y.LE..9).AND.(Y.GT..8)) IELM=6` | image, 350 dpi | The upper part is a loop closed on both sides (its left side runs 11 rows), then a waist, then the lower loop: its right side, its bottom and a 6-row fragment of its left side. This printer's 3 is flat-topped with a diagonal and has no upper-left side (08400, 09200, 09900). Reader A read .3 without a mark. With .8, the branch IELM=6 is taken with probability 0.1. |
| 164 | 08800 | `08800           G[?O\|J] TO 100` | `08800           GO TO 100` | `08800           GO TO 100` | context | Image: the second letter shows only a right side and a bottom (J-like); the 0s of 100 on the same line are broken the same way. Context: GJ TO 100 is not a statement; label 100 is at 09700. |
| 164 | 09800 | `09800           IOUT(N)=ISYM[?B\|R]L(IELM)` | `09800           IOUT(N)=ISYMBL(IELM)` | `09800           IOUT(N)=ISYMBL(IELM)` | context | Image: R-like, a bowl and a leg with no bottom bar, as on p. 163 04600. Context: the array ISYMBL (DIMENSION p. 163 04600, DATA 07100). |
| 164 | 10400 | `10400    900    FORMAT(/,/,1X,'> END OF RUN; INPUT [?D\|O]ATA WAS:',/)` | `10400    900    FORMAT(/,/,1X,'> END OF RUN; INPUT DATA WAS:',/)` | `10400    900    FORMAT(/,/,1X,'> END OF RUN; INPUT DATA WAS:',/)` | image, 350 dpi | Straight left side and a semicircular right side curving into the bottom: a D. The O of OF on the same line has rounded corners at the left. Context agrees: the message reads DATA. |
| 166 | 01700 | `01700           SUBROUTINE STATS(XIN1,XIN2,XIN3[?.\|,]XIN4,S1,` | `01700           SUBROUTINE STATS(XIN1,XIN2,XIN3[?.\|,]XIN4,S1,` | `01700           SUBROUTINE STATS(XIN1,XIN2,XIN3,XIN4,S1,` | image, 350 dpi | A blob 11 px wide (9-10 px per row) and 16 rows tall, leaning like a comma, with the tail unprinted. The commas on this page are 10-12 px wide; the periods are 13-14 px wide and rounder. Context agrees: a period in the dummy-argument list would be a syntax error. Both readers marked it. |
| 167 | 02900 | `02800   C       P(J)-     COMPUTED POSTERIOR PROB OF PATH` | `02900   C       P(J)-     COMPUTED POSTERIOR PROB OF PATH` | `02900   C       P(J)-     COMPUTED POSTERIOR PROB OF PATH` | image, 350 dpi | The third digit has a large upper loop and a right descender ending in a blob at the bottom. That is the structure of the 9 in the next line's 02900, with no lower-left side as an 8 would have. So the sequence number 02900 is printed twice (02700, 02900, 02900, 03000), an irregularity of the original. Reader A wrote 02800. |
| 168 | 07400 | `07400           CALL PATH(IPATH,LAMBDA(I),DUR(I),ILRATE(I),LAMSAV,DURSAV,ILR[?][text cut off at the right edge of the scan]` | `07400           CALL PATH(IPATH,LAMBDA(I),DUR(I),ILRATE(I),LAMSAV,DURSAV,ILR[?][cut off at right edge of scan]` | `07400           CALL PATH(IPATH,LAMBDA(I),DUR(I),ILRATE(I),LAMSAV,DURSAV,ILR[cut off at the scan's right edge]` | notation | Both readers read ...ILR and the cut. The reproduction of the printout ends at x = 2712 px of 2761 px on this page; no other line on the page reaches that far. Only a sliver of the next character's left edge is visible. The rest is not inferred. |
| 168 | 06900 | `08900` | `06900` | `06900` | image, 350 dpi | The second digit is a 6: an ascender, 5-7 px wide, from the top down the left side into a bowl, with no upper-right side as an 8 would have (compare the 8s of 08800 on the line above). So 06900 is printed where 08900 would follow 08800; 06900 also numbers the line 20 lines earlier. Both are irregularities of the original. Reader A wrote 08900. |
| 168 | 09900 | `09900    1000   FORMAT(//,4X,F1[?3\|2].7,/)` | `09900    1000   FORMAT(//,4X,F10.7,/)` | `09900    1000   FORMAT(//,4X,F10.7,/)` | image, 350 dpi | A rounded top with a left side, a right side in both halves, a slash fragment and a bottom fragment: a broken slashed zero (compare the 0 of XHAT=0 at 09000 and the 0s of 1000. on p. 167 06000). This printer's 3 is flat-topped with a middle notch, and a 2 has no right side in its lower half. Mean IoU of the 5 best matches: 0.46 against 0s, 0.41 against 3s, 0.35 against 2s. |
| 169 | {1}7000 | `[?]7000           DO 100 K=1,6` | `[?]7000           DO 100 K=1,[?6\|8]` | `{1}7000           DO 100 K=1,6` | context | Image: only a lower loop is printed, so it could be a 6 without its ascender or an 8 without its upper loop. Context: the loop body computes N=(I-1)*6+K, I=1..5, which indexes PIN(30) and P(25,30): 6 element types x 5 rates = 30 states. With 8, N would reach 32. |
| 170 | 19400 | `19400   C   VARIABLES: [stray mid-height dot after the colon, not a character]` | `19400   C   VARIABLES:` | `19400   C   VARIABLES:` | image, 350 dpi (not a character) | Reader A noted a stray dot two cells after the colon. It is 10 px high and 7 px wide, with 54 px of ink, at mid-height. Periods on this page have 135-192 px of ink and sit on the baseline. Not a character. |
| 175 | 49800 | `49800        2  DUR,ILRATE,PIN,LKHDJ)` | `49800        2  DUR,IL[?P\|R]ATE,PIN,LKHDJ)` | `49800        2  DUR,ILRATE,PIN,LKHDJ)` | context | Image: P-like (an R whose leg is unprinted). Context: ILRATE is LIKHD's own argument, used again at 50300, and it is the matching dummy argument of KALFIL (p. 176 00200). |
| 175 | 50300 | `50300           TYPE 1000,IP,Z,LAMBDA,K,ILRATE,ISRATE,DUR,PIN,LKHDJ,RN` | `50300           TYPE 1000,IP,Z,LAMBDA,K,ILRATE,ISRATE,DUR,PIN,LKH[?O\|D]J,RN` | `50300           TYPE 1000,IP,Z,LAMBDA,K,ILRATE,ISRATE,DUR,PIN,LKHDJ,RN` | context | Image: a rounded loop. D and O are not separable when broken in this print: mean IoU of the 5 best matches is 0.57 against D and 0.59 against O on pp. 174-177. Context: LKHDJ is KALFIL's output argument, used at 49800 and 50000; LKHOJ occurs nowhere. The statement is unreachable (it follows GO TO 100 at 50100 and has no label), so the program does not depend on it. |
| 177 | 08500 | `08500           TYPE 1000,Z,HZ,QA,PHI,PZ,ZR,G,PEST,YKK,YKKSV(JNODE),LKHDJ` | `08500           TYPE 1000,Z,HZ,QA,PHI,PZ,ZR,G,PEST,YKK,YKKSV(JNODE),LKH[?O\|D]J` | `08500           TYPE 1000,Z,HZ,QA,PHI,PZ,ZR,G,PEST,YKK,YKKSV(JNODE),LKHDJ` | context | Image: a rounded loop, D or O (mean IoU of the 5 best matches: 0.60 against D, 0.60 against O). Context: LKHDJ is declared REAL at p. 176 02800 and assigned at 08000 and 08300. The statement is unreachable (it follows GO TO 400 at 08400 and has no label). |
| 178 | 17000 | `[?]7000` | `17000` | `17000` | image, 350 dpi | The first digit's thin stem and base are visible, as legible as the 1 of 17100 on the next line, which both readers read. Reader A wrote [?]7000. |
| 180 | 24100 | `24100           DO 100 I=1,5` | `24100           [?D\|O]O 100 I=1,5` | `24100           DO 100 I=1,5` | context | Image: the first letter shows a left side, top and bottom with the right side unprinted (C-like). Context: this is the middle loop of DO 100 K / DO 100 I / DO 100 M, which ends at 100 CONTINUE (25100). OO 100 I=1,5 or CO 100 I=1,5 would not compile. |
| 180 | 27400 | `27400   C   SAVES ONLY ENOUGH TO MAKE THE TOTAL SAVED PROBABILITY [small raised speck between SAVED and PROBABILITY, not a character]` | `27400   C   SAVES ONLY ENOUGH TO MAKE THE TOTAL SAVED PROBABILITY` | `27400   C   SAVES ONLY ENOUGH TO MAKE THE TOTAL SAVED PROBABILITY` | image, 350 dpi (not a character) | Reader A noted a raised speck between SAVED and PROBABILITY. It is 5 px high and 6 px wide, with 24 px of ink, in the space, in the upper half of the line. Periods on this page have 150-196 px of ink. Not a character. |
| 180 | 27500 | `27500   C   E[?Q\|G]UAL TO POPT.` | `27500   C   EQUAL TO POPT.` | `27500   C   EQUAL TO POPT.` | context | Image: a broken glyph with fragments where both the G's bar and the Q's tail would be. Context: a comment; the same phrase, 'EQUAL TO POPT', appears in the comment at p. 181 33900 with a clear Q. The reasoning is about the words; the program does not depend on it. |
| 181 | 30600 | `30600           COMMON/BLKSV1/YKKIP,PKKIP,YKKSV,PKK[?S\|9]V` | `30600           COMMON/BLKSV1/YKKIP,PKKIP,YKKSV,PKKSV` | `30600           COMMON/BLKSV1/YKKIP,PKKIP,YKKSV,PKKSV` | image, 350 dpi | An S whose upper curve closes on the middle stroke, with the lower end curling up at the left like this page's S's. Mean IoU of the 5 best matches: 0.72 against S, 0.39 against 9 (pp. 180-182). Context agrees: PKKSV is dimensioned at 30300 and used at 38500. |
| 181 | 31100 | `31100           PSUM=[?0\|O].` | `31100           PSUM=0.` | `31100           PSUM=0.` | context | Image: a U-shaped glyph with an unprinted top; a slashed or plain oval cannot be told. Context: PSUM=O. is not valid Fortran (O. is no constant), PSUM=0. is. |
| 181 | 35000 | `35000           IF(P(J).LE.PMAX) GO TO 5[?3\|0]0` | `35000           IF(P(J).LE.PMAX) GO TO 500` | `35000           IF(P(J).LE.PMAX) GO TO 500` | image, 350 dpi | A rounded top whose left end curls down, then a right side and a bottom: a broken zero (compare the 0 next to it). This printer's 3 is flat-topped with a middle notch (p. 180 24600; p. 181 31900, 34500). Mean IoU of the 5 best matches: 0.56 against 0s, 0.40 against 3s. Context agrees: label 500 exists (35400) and SAVEP has no label 530. |
| 182 | 40200 | `40200           IF(LAMB[?D\|O]A(K).NE.LAMBDA(N)) GO TO 810` | `40200           IF(LAMBDA(K).NE.LAMBDA(N)) GO TO 810` | `40200           IF(LAMBDA(K).NE.LAMBDA(N)) GO TO 810` | context | Image: an oval with a flat bottom, O-like. Context: LAMBDA is the array (DIMENSION p. 181 30100) and appears as LAMBDA(N) in the same statement; LAMBOA(K) would be an undefined function. |
| 184 | 04120 | `04100           XDLAVG=(XDLAVG*(NCALL-1)+NDEL)/NCALL` | `04120           XDLAVG=(XDLAVG*(NCALL-1)+NDEL)/NCALL` | `04120           XDLAVG=(XDLAVG*(NCALL-1)+NDEL)/NCALL` | image, 350 dpi | The fourth digit is a 2 (top arc, diagonal, flat base), like the 2 of 04200 on the next line. The printed numbers run 04000, 04120, 04200. Reader A wrote 04100. |
| 185 | 09700 | `09700           IF(NDEL.EQ.NDELST+1) GO TO 800` | `09700           IF(NDEL.EQ.NDELST+1) GO TO 900` | `09700           IF(NDEL.EQ.NDELST+1) GO TO 800` | image, 350 dpi | A closed upper loop, a right descender and a fragment of the lower loop's left side: an 8. Best-match IoU: 0.68 against 8s, 0.46 against 9s (pp. 184-186; only 2 nines). Context agrees: TRELIS has label 800 (16000) and no label 900. |
| 186 | 15500 | `15500           IF(IPNOD(K).EQ.IPMAX) GO TO 750 [small dot before IF in column 8, about half the size of a printed period]` | `15500           IF(IPNOD(K).EQ.IPMAX) GO TO 750` | `15500           IF(IPNOD(K).EQ.IPMAX) GO TO 750` | image, 350 dpi (not a character) | Reader A noted a small dot before IF. It is 6 px high and 5 px wide, with 24 px of ink, on the baseline. Periods on this page have 150-193 px of ink. Not a character; context agrees (.IF would not compile). |
| 187 | 22900 | `22900           IF(NBLANK.EQ.[?2\|0]) GO TO 110` | `22900           IF(NBLANK.EQ.0) GO TO 110` | `22900           IF(NBLANK.EQ.0) GO TO 110` | image, 350 dpi | A broken slashed zero: thick top right, a straight slash, fragments of the left side and bottom. Mean IoU of the 5 best matches: 0.51 against 0s, 0.34 against 2s. The parallel tests at 24100 and 25300 read NBLANK.EQ.0. |
| 187 | 23700 | `23700    2[?0\|3]0 NCHAR=NCHAR+2` | `23700    200    NCHAR=NCHAR+2` | `23700    200    NCHAR=NCHAR+2` | context | Image: the middle digit of the label is broken: a top-right part, a slash fragment, a left-side fragment in rows 40-44 and a bottom. That leans to 0, since a 3 has no left side there. Context: IF(LTRMAP(LTRHAT).LE.46) GO TO 200 (21100) needs label 200, and TRANSL has no other. Layout: the statement starts in the statement column, as in reader B (image). |
| 188 | 25800 | `25800           GO TO 600   [small dot in column 19, about half the size of a printed period]` | `25800           GO TO 600` | `25800           GO TO 600` | image, 350 dpi (not a character) | Reader A noted a small dot after 600. It is 7 px high and 9 px wide, with 49 px of ink, near the baseline. Periods on this page have 135-199 px of ink. Not a character. |
| 189 | 04400 | `04400   C   1000 HZ. THE LPF OF THE ENVELOPE DETECTOR IS A [tiny mid-height speck between DETECTOR and IS, not a character]` | `04400   C   1000 HZ. THE LPF OF THE ENVELOPE DETECTOR IS A` | `04400   C   1000 HZ. THE LPF OF THE ENVELOPE DETECTOR IS A` | image, 350 dpi (not a character) | Reader A noted a speck between DETECTOR and IS. It is 5 px high and 5 px wide, with 19 px of ink, at mid-height, in the space. Periods on this page have 151-205 px of ink. Not a character. |
| 189 | 05200 | `05200           DATA A/.000[?0\|4]30051,2.9507982,2.90396345,-.953135172/` | `05200           DATA A/.000030051,2.9507982,2.90396345,-.953135172/` | `05200           DATA A/.000030051,2.9507982,2.90396345,-.953135172/` | image, 350 dpi | The tops of the left stroke and the right stroke join (an inverted V), and there is no crossbar. This printer's 4s (p. 188 26700; the 4 of 2.90396345 in this line; p. 189 05300; p. 190 08700) have separate tops and a crossbar at two-thirds height. So this is a broken slashed zero. Mean IoU of the 5 best matches: 0.53 against 0s, 0.35 against 4s. Consistency (derived): the low-pass filter at 08100-08700 is Z = A(1)(X + 3X1 + 3X2 + X3) + A(2)Z1 - A(3)Z2 - A(4)Z3. Since 1 - A(2) + A(3) + A(4) = 0.000030078, A(1) = 0.000030051 gives a DC gain of 7.99 (8 x 0.9991), while 0.000430051 would give 114. |
| 190 | 07100 | `07100   C      [tiny speck below the baseline in column 5, not a character]` | `07100   C` | `07100   C` | image, 350 dpi (not a character) | Reader A noted a speck below the baseline. It is 5 px high and 5 px wide, with 22 px of ink, at the baseline. Periods on this page have 128-182 px of ink. Not a character. |
| 190 | 08100 | `08100           Y0=XDET*A(1)` | `08100           Y0=X[?D\|O]ET*A(1)` | `08100           Y0=XDET*A(1)` | context | Image: a rounded loop, O-like. Context: XDET is assigned at 07200 (XDET=SQRT(ZBPF**2)). With XOET the filter would read an undefined variable and BPFDET would put out no signal. |
| 190 | 08900 | `08900             [small dot at mid-height in column 9, about half the size of a printed period]` | `08900` | `08900` | image, 350 dpi (not a character) | Reader A noted a small dot at mid-height. It is 8 px high and 6 px wide, with 39 px of ink, in the upper half of an empty line. Periods on this page have 128-182 px of ink. Not a character. |

## Sequence numbers cut off at the left edge

The scan's left edge cuts off the first digit of the sequence numbers on these lines (only a sliver of it shows, if
anything). The numbers step by 100 within a numbering group.

- p. 159: the main program is the only routine in its numbering group, so no neighbor fixes the digit. It is probably
  0, because each of the seven other groups in the listing starts at 00100, but that is a convention, not certainty.
  Kept as `[?]`.
- p. 163: `{0}`. SIMSG1 starts its group at x0100, and KEY continues the same group on p. 164 from 06100
  (legible). So the 60 numbers x0100-x6000 are 00100-06000. A sliver of the digit's right edge shows on most lines,
  consistent with 0.
- p. 169: `{1}`. p. 168 ends at 11900 and p. 170 starts at 18000 (both legible). So the 60 numbers x2000-x7900 are
  12000-17900.
- p. 177: `{1}`. The line before is 10900 (legible) and p. 178 starts at 12100. So x1000-x2000 is 11000-12000.
- p. 178: `{1}`. The line before is 17100 (legible) and p. 179 starts at 18100. So x7200-x8000 is 17200-18000. On the
  line between 16900 and 17100, the 1 is legible (17000): see the character table.

Lines of these pages with a character difference are in the character table above. Their digit is settled the
same way.

| Page | Reader A | Reader B | Settled | Class |
|---|---|---|---|---|
| 159 | `[?]0100` | `[?]0100` | `[?]0100` | kept |
| 159 | `[?]0200` | `[?]0200` | `[?]0200` | kept |
| 159 | `[?]0300` | `[?]0300` | `[?]0300` | kept |
| 159 | `[?]0400` | `[?]0400` | `[?]0400` | kept |
| 159 | `[?]0500` | `[?]0500` | `[?]0500` | kept |
| 159 | `[?]0600` | `[?]0600` | `[?]0600` | kept |
| 159 | `[?]0700` | `[?]0700` | `[?]0700` | kept |
| 159 | `[?]0800` | `[?]0800` | `[?]0800` | kept |
| 159 | `[?]0900` | `[?]0900` | `[?]0900` | kept |
| 159 | `[?]1000` | `[?]1000` | `[?]1000` | kept |
| 159 | `[?]1100` | `[?]1100` | `[?]1100` | kept |
| 159 | `[?]1200` | `[?]1200` | `[?]1200` | kept |
| 159 | `[?]1300` | `[?]1300` | `[?]1300` | kept |
| 159 | `[?]1400` | `[?]1400` | `[?]1400` | kept |
| 159 | `[?]1500` | `[?]1500` | `[?]1500` | kept |
| 159 | `[?]1600` | `[?]1600` | `[?]1600` | kept |
| 159 | `[?]1700` | `[?]1700` | `[?]1700` | kept |
| 159 | `[?]1800` | `[?]1800` | `[?]1800` | kept |
| 159 | `[?]1900` | `[?]1900` | `[?]1900` | kept |
| 159 | `[?]2000` | `[?]2000` | `[?]2000` | kept |
| 159 | `[?]2100` | `[?]2100` | `[?]2100` | kept |
| 159 | `[?]2200` | `[?]2200` | `[?]2200` | kept |
| 159 | `[?]2300` | `[?]2300` | `[?]2300` | kept |
| 159 | `[?]2400` | `[?]2400` | `[?]2400` | kept |
| 159 | `[?]2500` | `[?]2500` | `[?]2500` | kept |
| 159 | `[?]2600` | `[?]2600` | `[?]2600` | kept |
| 159 | `[?]2700` | `[?]2700` | `[?]2700` | kept |
| 159 | `[?]2800` | `[?]2800` | `[?]2800` | kept |
| 159 | `[?]3000` | `[?]3000` | `[?]3000` | kept |
| 159 | `[?]3100` | `[?]3100` | `[?]3100` | kept |
| 163 | `[?]0100` | `00100` | `{0}0100` | neighbors |
| 163 | `[?]0200` | `00200` | `{0}0200` | neighbors |
| 163 | `[?]0300` | `00300` | `{0}0300` | neighbors |
| 163 | `[?]0400` | `00400` | `{0}0400` | neighbors |
| 163 | `[?]0500` | `00500` | `{0}0500` | neighbors |
| 163 | `[?]0600` | `00600` | `{0}0600` | neighbors |
| 163 | `[?]0700` | `00700` | `{0}0700` | neighbors |
| 163 | `[?]0800` | `00800` | `{0}0800` | neighbors |
| 163 | `[?]0900` | `00900` | `{0}0900` | neighbors |
| 163 | `[?]1000` | `01000` | `{0}1000` | neighbors |
| 163 | `[?]1100` | `01100` | `{0}1100` | neighbors |
| 163 | `[?]1200` | `01200` | `{0}1200` | neighbors |
| 163 | `[?]1300` | `01300` | `{0}1300` | neighbors |
| 163 | `[?]1400` | `01400` | `{0}1400` | neighbors |
| 163 | `[?]1500` | `01500` | `{0}1500` | neighbors |
| 163 | `[?]1600` | `01600` | `{0}1600` | neighbors |
| 163 | `[?]1700` | `01700` | `{0}1700` | neighbors |
| 163 | `[?]1800` | `01800` | `{0}1800` | neighbors |
| 163 | `[?]1900` | `01900` | `{0}1900` | neighbors |
| 163 | `[?]2000` | `02000` | `{0}2000` | neighbors |
| 163 | `[?]2100` | `02100` | `{0}2100` | neighbors |
| 163 | `[?]2200` | `02200` | `{0}2200` | neighbors |
| 163 | `[?]2400` | `02400` | `{0}2400` | neighbors |
| 163 | `[?]2500` | `02500` | `{0}2500` | neighbors |
| 163 | `[?]2600` | `02600` | `{0}2600` | neighbors |
| 163 | `[?]2700` | `02700` | `{0}2700` | neighbors |
| 163 | `[?]2800` | `02800` | `{0}2800` | neighbors |
| 163 | `[?]2900` | `02900` | `{0}2900` | neighbors |
| 163 | `[?]3000` | `03000` | `{0}3000` | neighbors |
| 163 | `[?]3100` | `03100` | `{0}3100` | neighbors |
| 163 | `[?]3300` | `03300` | `{0}3300` | neighbors |
| 163 | `[?]3400` | `03400` | `{0}3400` | neighbors |
| 163 | `[?]3500` | `03500` | `{0}3500` | neighbors |
| 163 | `[?]3700` | `03700` | `{0}3700` | neighbors |
| 163 | `[?]3800` | `03800` | `{0}3800` | neighbors |
| 163 | `[?]3900` | `03900` | `{0}3900` | neighbors |
| 163 | `[?]4000` | `04000` | `{0}4000` | neighbors |
| 163 | `[?]4100` | `04100` | `{0}4100` | neighbors |
| 163 | `[?]4200` | `04200` | `{0}4200` | neighbors |
| 163 | `[?]4300` | `04300` | `{0}4300` | neighbors |
| 163 | `[?]4400` | `04400` | `{0}4400` | neighbors |
| 163 | `[?]4500` | `04500` | `{0}4500` | neighbors |
| 163 | `[?]4700` | `04700` | `{0}4700` | neighbors |
| 163 | `[?]4900` | `04900` | `{0}4900` | neighbors |
| 163 | `[?]5100` | `05100` | `{0}5100` | neighbors |
| 163 | `[?]5200` | `05200` | `{0}5200` | neighbors |
| 163 | `[?]5400` | `05400` | `{0}5400` | neighbors |
| 163 | `[?]5500` | `05500` | `{0}5500` | neighbors |
| 163 | `[?]5600` | `05600` | `{0}5600` | neighbors |
| 163 | `[?]5700` | `05700` | `{0}5700` | neighbors |
| 163 | `[?]5800` | `05800` | `{0}5800` | neighbors |
| 163 | `[?]6000` | `06000` | `{0}6000` | neighbors |
| 169 | `[?]2000` | `[?]2000` | `{1}2000` | neighbors |
| 169 | `[?]2100` | `[?]2100` | `{1}2100` | neighbors |
| 169 | `[?]2200` | `[?]2200` | `{1}2200` | neighbors |
| 169 | `[?]2300` | `[?]2300` | `{1}2300` | neighbors |
| 169 | `[?]2400` | `[?]2400` | `{1}2400` | neighbors |
| 169 | `[?]2500` | `[?]2500` | `{1}2500` | neighbors |
| 169 | `[?]2600` | `[?]2600` | `{1}2600` | neighbors |
| 169 | `[?]2700` | `[?]2700` | `{1}2700` | neighbors |
| 169 | `[?]2800` | `[?]2800` | `{1}2800` | neighbors |
| 169 | `[?]2900` | `[?]2900` | `{1}2900` | neighbors |
| 169 | `[?]3000` | `[?]3000` | `{1}3000` | neighbors |
| 169 | `[?]3100` | `[?]3100` | `{1}3100` | neighbors |
| 169 | `[?]3200` | `[?]3200` | `{1}3200` | neighbors |
| 169 | `[?]3300` | `[?]3300` | `{1}3300` | neighbors |
| 169 | `[?]3400` | `[?]3400` | `{1}3400` | neighbors |
| 169 | `[?]3500` | `[?]3500` | `{1}3500` | neighbors |
| 169 | `[?]3600` | `[?]3600` | `{1}3600` | neighbors |
| 169 | `[?]3700` | `[?]3700` | `{1}3700` | neighbors |
| 169 | `[?]3800` | `[?]3800` | `{1}3800` | neighbors |
| 169 | `[?]3900` | `[?]3900` | `{1}3900` | neighbors |
| 169 | `[?]4000` | `[?]4000` | `{1}4000` | neighbors |
| 169 | `[?]4100` | `[?]4100` | `{1}4100` | neighbors |
| 169 | `[?]4200` | `[?]4200` | `{1}4200` | neighbors |
| 169 | `[?]4300` | `[?]4300` | `{1}4300` | neighbors |
| 169 | `[?]4400` | `[?]4400` | `{1}4400` | neighbors |
| 169 | `[?]4500` | `[?]4500` | `{1}4500` | neighbors |
| 169 | `[?]4600` | `[?]4600` | `{1}4600` | neighbors |
| 169 | `[?]4700` | `[?]4700` | `{1}4700` | neighbors |
| 169 | `[?]4800` | `[?]4800` | `{1}4800` | neighbors |
| 169 | `[?]4900` | `[?]4900` | `{1}4900` | neighbors |
| 169 | `[?]5000` | `[?]5000` | `{1}5000` | neighbors |
| 169 | `[?]5100` | `[?]5100` | `{1}5100` | neighbors |
| 169 | `[?]5200` | `[?]5200` | `{1}5200` | neighbors |
| 169 | `[?]5300` | `[?]5300` | `{1}5300` | neighbors |
| 169 | `[?]5400` | `[?]5400` | `{1}5400` | neighbors |
| 169 | `[?]5500` | `[?]5500` | `{1}5500` | neighbors |
| 169 | `[?]5600` | `[?]5600` | `{1}5600` | neighbors |
| 169 | `[?]5700` | `[?]5700` | `{1}5700` | neighbors |
| 169 | `[?]5800` | `[?]5800` | `{1}5800` | neighbors |
| 169 | `[?]5900` | `[?]5900` | `{1}5900` | neighbors |
| 169 | `[?]6000` | `[?]6000` | `{1}6000` | neighbors |
| 169 | `[?]6100` | `[?]6100` | `{1}6100` | neighbors |
| 169 | `[?]6200` | `[?]6200` | `{1}6200` | neighbors |
| 169 | `[?]6300` | `[?]6300` | `{1}6300` | neighbors |
| 169 | `[?]6400` | `[?]6400` | `{1}6400` | neighbors |
| 169 | `[?]6500` | `[?]6500` | `{1}6500` | neighbors |
| 169 | `[?]6600` | `[?]6600` | `{1}6600` | neighbors |
| 169 | `[?]6700` | `[?]6700` | `{1}6700` | neighbors |
| 169 | `[?]6800` | `[?]6800` | `{1}6800` | neighbors |
| 169 | `[?]6900` | `[?]6900` | `{1}6900` | neighbors |
| 169 | `[?]7100` | `[?]7100` | `{1}7100` | neighbors |
| 169 | `[?]7200` | `[?]7200` | `{1}7200` | neighbors |
| 169 | `[?]7300` | `[?]7300` | `{1}7300` | neighbors |
| 169 | `[?]7400` | `[?]7400` | `{1}7400` | neighbors |
| 169 | `[?]7500` | `[?]7500` | `{1}7500` | neighbors |
| 169 | `[?]7600` | `[?]7600` | `{1}7600` | neighbors |
| 169 | `[?]7700` | `[?]7700` | `{1}7700` | neighbors |
| 169 | `[?]7800` | `[?]7800` | `{1}7800` | neighbors |
| 169 | `[?]7900` | `[?]7900` | `{1}7900` | neighbors |
| 177 | `[?]1000` | `[?]1000` | `{1}1000` | neighbors |
| 177 | `[?]1100` | `[?]1100` | `{1}1100` | neighbors |
| 177 | `[?]1200` | `[?]1200` | `{1}1200` | neighbors |
| 177 | `[?]1300` | `[?]1300` | `{1}1300` | neighbors |
| 177 | `[?]1400` | `[?]1400` | `{1}1400` | neighbors |
| 177 | `[?]1500` | `[?]1500` | `{1}1500` | neighbors |
| 177 | `[?]1600` | `[?]1600` | `{1}1600` | neighbors |
| 177 | `[?]1700` | `[?]1700` | `{1}1700` | neighbors |
| 177 | `[?]1800` | `[?]1800` | `{1}1800` | neighbors |
| 177 | `[?]1900` | `[?]1900` | `{1}1900` | neighbors |
| 177 | `[?]2000` | `[?]2000` | `{1}2000` | neighbors |
| 178 | `[?]7200` | `[?]7200` | `{1}7200` | neighbors |
| 178 | `[?]7300` | `[?]7300` | `{1}7300` | neighbors |
| 178 | `[?]7400` | `[?]7400` | `{1}7400` | neighbors |
| 178 | `[?]7500` | `[?]7500` | `{1}7500` | neighbors |
| 178 | `[?]7600` | `[?]7600` | `{1}7600` | neighbors |
| 178 | `[?]7700` | `[?]7700` | `{1}7700` | neighbors |
| 178 | `[?]7800` | `[?]7800` | `{1}7800` | neighbors |
| 178 | `[?]7900` | `[?]7900` | `{1}7900` | neighbors |
| 178 | `[?]8000` | `[?]8000` | `{1}8000` | neighbors |
