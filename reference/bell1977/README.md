# Bell 1977: the Fortran listing

This folder holds E. L. Bell's Fortran program, transcribed from the "Computer Programs" appendix of his thesis:

> E. L. Bell, "Optimal Bayesian Estimation of the State of a Probabilistically Mapped Memory-Conditional Markov
> Process with Application to Manual Morse Decoding", Doctor of Engineering thesis, Naval Postgraduate School,
> Monterey, California, September 1977.

| File | What it is |
|---|---|
| `listing.txt` | The listing, transcribed verbatim, page by page. |
| `transcription-log.md` | How the two readings were settled. It has one row per line that differed or carried a mark, the reason for each decision, and the counts. |

The transcription keeps the original exactly, including its apparent errors. For example, it keeps the misspelling
MINIIMUM in a comment on p. 180 (27100), and the DATA row for C on p. 163 (05300), which repeats the row for B. A later
task will add a version made to compile, with each change marked.

## Page map

"p. N" is the page number printed on the thesis page. In the scan used (404 PDF pages), printed page N is PDF page
2N + 3; the even PDF pages between the listing pages are blank versos.

| Printed pages | PDF pages | Content |
|---|---|---|
| 158 | 319 | Figure 18i, "Output Waveforms", printed rotated 90 degrees. It is not part of the listing; `listing.txt` gives a one-line description and the figure's labels. |
| 159-191 | 321-385 | The listing. p. 159 is headed "COMPUTER PROGRAMS". |

Bell's sequence numbers are the line numbers of the DEC editor, not part of the Fortran. They fall into eight numbering
groups. Every group after the first starts at 00100 (in the first, on p. 159, the first digit is cut off). Within a
group the numbers step by 100, with the exceptions listed in `transcription-log.md`.

| Group | Pages | Sequence numbers | Routines: page and sequence number where each starts |
|---|---|---|---|
| 1 | 159 | `[?]0100`-`[?]3100` | main program (p. 159) |
| 2 | 160-162 | 00100-13500 | INPUTL (p. 160, 00100); INITL (p. 160, 04900) |
| 3 | 163-165 | `{0}0100`-12300 | SIMSG1 (p. 163, `{0}0100`); KEY (p. 163, `{0}4400`) |
| 4 | 166 | 00100-05600 | DISPLA (00100); STATS (01700); AUTOCR (03200) |
| 5 | 167-175 | 00100-51200 | PROCES (p. 167, 00100); TRPROB (p. 169, `{1}2300`); XTRANS, a function (p. 170, 18600); PTRANS (p. 171, 24600); SPDTR, a function (p. 172, 30800); PATH (p. 173, 36400); LIKHD (p. 174, 44100) |
| 6 | 176-183 | 00100-43200 | KALFIL (p. 176, 00100); MODEL (p. 177, 09500); PROBP (p. 178, 15100); SPROB (p. 179, 20900); SAVEP (p. 180, 26400) |
| 7 | 184-188 | 00100-29500 | TRELIS (p. 184, 00100); TRANSL (p. 186, 16800) |
| 8 | 189-191 | 00100-15000 | RCVR (p. 189, 00100); BPFDET (p. 189, 03700); NOISE (p. 190, 09600) |

## Method

- **Two readings.** Two readers, separate agents, transcribed every page from images of the scan independently.
  Neither saw the other's work. Neither used the PDF's text layer, which garbles code.
  - The images were rendered with Ghostscript, for example
    `gswin64c -q -dNOPAUSE -dBATCH -sDEVICE=pnggray -r170 -dFirstPage=M -dLastPage=M -sOutputFile=pM.png <thesis PDF>`.
  - The readers worked at 170 dpi and zoomed at 300 dpi and above.
- **Agreement.** The two readings agree on 1 715 of the 1 814 lines.
- **Settling.** Every difference, and every character that either reader marked as uncertain, was settled on the page
  image.
  - Each page was rendered at the scan's native resolution, about 350 dpi, at which the rendering reproduces the scan's
    text bitmap pixel for pixel.
  - Each doubtful glyph was compared with the same printer's glyphs on the same pages.
  - Where the image could not decide, the Fortran context decided: a statement label that must exist, a variable name
    used elsewhere, or the syntax.
- **The log.** `transcription-log.md` gives each decision with its reason, and the counts.
- **Kept out of the repository.** The page images and the two readings are working files and are not committed. The
  log quotes both readings for every line it lists.

## Conventions

- **Lines.** One line of `listing.txt` is one printed line. `=== p. N (PDF page M) ===` starts each page.
  `--- page number: N ---` is the page number printed at its foot.
- **Columns.**
  - Each line keeps the printed columns, at 10 characters per inch: first the sequence number, then the label,
    continuation mark and statement in their printed positions.
  - The file has no tabs and no trailing spaces.
- **Zeros.** Bell's slashed zero is written `0`. The letter O is written `O`.
- **Marks.**
  - `[?]`: a character that cannot be read. One is left on each of the 31 numbered lines of p. 159: the first digit of
    the sequence number, which the scan's left edge cuts off. It is probably 0, but no neighbor fixes it.
  - `[?x|y]`: a character that is x or y. None is left.
  - `{d}`: a digit cut off by the scan's left edge and reconstructed with certainty from the neighboring sequence
    numbers. There are 140, all the first digit of a sequence number, on pp. 163, 169, 177 and 178.
  - `[cut off at the scan's right edge]`: the rest of the line is not in the scan. This occurs once, on p. 168 at line
    07400, after `...,ILR`.
  - `[?]`, `[?x|y]` and `{d}` each stand for one printed column. The text after one of them is shifted to the right by
    its extra characters.
- **Descriptions.** A line in brackets on its own, on p. 158, is a description, not a transcription.
- **Sequence numbers** are kept as printed, including the irregular ones (02900 twice on p. 167; 06900 where 08900
  would follow on p. 168; 04120 on p. 184; others listed in the log).
- **Unusual glyph.** The character set is uppercase ASCII. One glyph is unusual: an underscore printed with a central
  tick, in `1H_` on p. 164 (07100). See the log.
