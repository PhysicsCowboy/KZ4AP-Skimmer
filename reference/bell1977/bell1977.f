C KZ4AP: bell1977.f -- E. L. Bell's program (thesis, Naval Postgraduate
C KZ4AP: School, 1977, pp. 159-191), from the settled transcription
C KZ4AP: listing.txt, made to compile with gfortran (build.sh). Columns
C KZ4AP: 73-80 hold Bell's sequence number of each line. Every change is
C KZ4AP: marked "C KZ4AP: change N" with its reason; the changes are:
C KZ4AP:  1 TYPE/ACCEPT: settings read from the file SETTINGS, output to
C KZ4AP:    standard output, operator waits removed, session length set;
C KZ4AP:  2 RAN and DATA IK: gfortran RANDOM_NUMBER, seeded from SETTINGS;
C KZ4AP:    RANDN (a missing library routine): Box-Muller;
C KZ4AP:  3 TRANSL's display output written to the file TRANSL.TXT;
C KZ4AP:  4 trace option (output only): EVENTS.TXT, SIGNAL.BIN, STEPS.BIN;
C KZ4AP:  5 MORSEM written by kz4ap-bell-tables (no code change here);
C KZ4AP:  6 LIKHD: guard for a dead path (LAMBDA = 0) before 47800;
C KZ4AP:  7 five jumps to a DO loop's terminal statement from outside
C KZ4AP:    the loop redirected to a new label after the loop;
C KZ4AP:  8 stubs for the display library routines ERASE, PLOTR, VIEW;
C KZ4AP:  9 KEY's duration law selectable: uniform as printed (LAW 0) or
C KZ4AP:    the text's truncated Gaussian, reading C (LAW 1);
C KZ4AP: 10 golden input 3 only: a rate change at a set time (off: 0).
C KZ4AP: See README.md for the files, the settings and the reasons.
      INTEGER ELMHAT,XHAT                                               00100
      DIMENSION S1(512),S2(512),S3(512)                                 00200
      DIMENSION S4(512)                                                 00300
      DATA RN/.1/                                                       00400
      DATA NP/0/                                                        00500
C KZ4AP: changes 1, 4 and 10: the settings (INPUTL) and the rate
C KZ4AP: change of golden input 3 (BLK6's DMEAN, which KEY reads).
      COMMON/KZ4AP/RATE2,KZTRC,KZLAW,NSTOP,NSW,KZSTP
      COMMON/BLK6/DMEAN,XDUR6,ESEP6(6),EDEV6(6)
C                                                                       00600
      CALL INITL                                                        00700
      CALL INPUTL                                                       00800
C                                                                       00900
1     DO 2 N1=1,512                                                     01000
      DO 3 N2=1,18                                                      01100
C KZ4AP: change 1: the operator ended a session; here it ends after
C KZ4AP: NSTOP samples at 8 kHz (SETTINGS). KZN counts the samples.
      IF(KZN.GE.NSTOP) GO TO 9000
      KZN=KZN+1
C KZ4AP: change 10: golden input 3 only; KEY's later elements are
C KZ4AP: keyed at RATE2 WPM from sample NSW on (NSW = 0: never).
      IF(KZN.EQ.NSW) DMEAN=1200./RATE2
      CALL SIMSG1(X,ZSIG)                                               01200
C                                                                       01300
      CALL RCVR(ZSIG,ZRCV)                                              01400
      CALL BPFDET(ZRCV,ZDET)                                            01500
C KZ4AP: change 4: trace of the 8 kHz signal s_n (AU), the key state
C KZ4AP: x_n and the envelope Z (AU), float64 each, stream file.
      IF(KZTRC.GE.2) WRITE(24) ZSIG,X,ZDET
C                                                                       01600
      NP=NP+1                                                           01700
      IF(NP.LT.40) GO TO 3                                              01800
      NP=0                                                              01900
      CALL NOISE(ZDET,RN,Z)                                             02000
C KZ4AP: change 4: KZSTP counts the decoder's 5 ms steps (trace only).
      KZSTP=KZSTP+1
      CALL PROCES(Z,RN,XHAT,PX,ELMHAT,LTRHAT)                           02100
3     CONTINUE                                                          02200
C                                                                       02300
      N=N1                                                              02400
      CALL STATS(ZDET,Z   ,PX  ,XHAT,S1,S2,S3,S4,N)                     02500
2     CONTINUE                                                          02600
      CALL DISPLA(S1,S2,S3,S4)                                          02700
C                                                                       02800
      GO TO 1                                                           02900
C KZ4AP: change 1: the end of the session; the output files closed.
9000  CLOSE(21)
      IF(KZTRC.GE.1) CLOSE(22)
      IF(KZTRC.GE.2) CLOSE(23)
      IF(KZTRC.GE.2) CLOSE(24)
      STOP                                                              03000
      END                                                               03100
      SUBROUTINE INPUTL                                                 00100
      DIMENSION ESEP(6),EDEV(6)                                         00200
      COMMON/BLK1/TAU/BLK6/DMEAN,XDUR,ESEP,EDEV                         00300
      COMMON/BLK2/WC,WCHIRP,ASIGMA,BSIGMA,PHISGM,                       00400
     2RSIGM,TCHIRP,GAMMA                                                00500
      DATA TAU/.000125/,ESEP/1,3,1,3,7,14/,EDEV/6*0./                   00600
      DATA XDUR/0./                                                     00700
C KZ4AP: change 1: the settings come from the file SETTINGS, its first
C KZ4AP: four lines as Bell typed them at the four prompts (any
C KZ4AP: separators; list-directed READ), then one line of ours:
C KZ4AP: seed, KEY's law (0 or 1), session length (s), trace level
C KZ4AP: (0, 1 or 2), rate-change time (s, 0: none), its rate (WPM).
      COMMON/KZ4AP/RATE2,KZTRC,KZLAW,NSTOP,NSW,KZSTP
      INTEGER KZSD(100)
      DIMENSION WARM(1000)
      OPEN(UNIT=10,FILE='SETTINGS',STATUS='OLD')
C                                                                       00800
C                                                                       00900
      PRINT 100                                                         01000
100   FORMAT(1X,'INPUT KEYING PARMS: RATE,MEAN ELEM DURATIONS')         01100
      READ(10,*) RATE,(ESEP(K),K=1,6)                                   01200
      PRINT 150                                                         01300
150   FORMAT(1X,'INPUT ELEM DURATION STD DEVIATIONS')                   01400
      READ(10,*) (EDEV(K),K=1,6)                                        01500
C KZ4AP: change 1: FORMAT 200 served only the ACCEPTs; it is kept.
200   FORMAT(7F)                                                        01600
      PRINT 300                                                         01700
300   FORMAT(1X,'INPUT SIG PARMS- AVAR,BVAR,FCHIRP,TCHIRP,PHIVAR')      01800
      READ(10,*) AVAR,BVAR,FCHIRP,TCHIRP,PHIVAR                         01900
      PRINT 400                                                         02000
400   FORMAT(1X,'INPUT SIG PARMS: GAMMA,FREQ,NOISE')                    02100
      READ(10,*) GAMMA,FC,RNOISE                                        02200
      READ(10,*) KZSEED,KZLAW,TSTOP,KZTRC,TSW,RATE2
      CLOSE(10)
      PRINT *,'RATE ',RATE,' ESEP ',ESEP
      PRINT *,'EDEV ',EDEV
      PRINT *,'AVAR ',AVAR,' BVAR ',BVAR,' FCHIRP ',FCHIRP,
     2  ' TCHIRP ',TCHIRP,' PHIVAR ',PHIVAR
      PRINT *,'GAMMA ',GAMMA,' FREQ ',FC,' NOISE ',RNOISE
      PRINT *,'SEED ',KZSEED,' LAW ',KZLAW,' TSTOP ',TSTOP,
     2  ' TRACE ',KZTRC,' TSW ',TSW,' RATE2 ',RATE2
      NSTOP=NINT(TSTOP/TAU)
      NSW=NINT(TSW/TAU)
C KZ4AP: change 2: one random stream for RAN and RANDN, seeded here.
      CALL RANDOM_SEED(SIZE=KZNS)
      IF(KZNS.GT.100) STOP 'RANDOM_SEED SIZE'
      DO 9010 I=1,KZNS
      KZSD(I)=INT(MOD(INT(KZSEED,8)*69069_8+INT(I,8)*1234567_8,
     2  2147483647_8))
9010  CONTINUE
      CALL RANDOM_SEED(PUT=KZSD(1:KZNS))
C KZ4AP: change 2: the first 1000 numbers are discarded: gfortran's
C KZ4AP: generator (xoshiro256**) starts from neighboring seeds with
C KZ4AP: correlated first outputs; its state mixes within that many.
      CALL RANDOM_NUMBER(WARM)
C KZ4AP: changes 3 and 4: the output files.
      OPEN(UNIT=21,FILE='TRANSL.TXT',STATUS='REPLACE')
      IF(KZTRC.GE.1) OPEN(UNIT=22,FILE='EVENTS.TXT',STATUS='REPLACE')
      IF(KZTRC.GE.2) OPEN(UNIT=23,FILE='STEPS.BIN',STATUS='REPLACE',
     2  ACCESS='STREAM',FORM='UNFORMATTED')
      IF(KZTRC.GE.2) OPEN(UNIT=24,FILE='SIGNAL.BIN',STATUS='REPLACE',
     2  ACCESS='STREAM',FORM='UNFORMATTED')
C                                                                       02300
      ASIGMA=SQRT(AVAR)                                                 02400
      BSIGMA=SQRT(BVAR)                                                 02500
      PHISGM=SQRT(PHIVAR)                                               02600
      RSIGM=SQRT(RNOISE)                                                02700
C                                                                       02800
      DMEAN=1200./RATE                                                  02900
      WC=6.28319*FC                                                     03000
      WCHIRP=6.28319*FCHIRP                                             03100
C                                                                       03200
C                                                                       03300
      IF(ESEP(1).NE.0.) GO TO 500                                       03400
      ESEP(1)=1.                                                        03500
      ESEP(2)=3.                                                        03600
      ESEP(3)=1.                                                        03700
      ESEP(4)=3.                                                        03800
      ESEP(5)=7.                                                        03900
      ESEP(6)=14.                                                       04000
C                                                                       04100
C                                                                       04200
500   RETURN                                                            04300
      END                                                               04400
C                                                                       04500
C                                                                       04600
C                                                                       04700
C                                                                       04800
      SUBROUTINE INITL                                                  04900
      DIMENSION IELMST(400),ILAM1(16),ILAMX(6)                          05000
      DIMENSION ELEMTR(16,6),RTRANS(5,2),ISX(6)                         05100
      DIMENSION MEMFCN(400,6),LTRMAP(400),IALPH(70)                     05200
      DIMENSION MEMDEL(6,6),MEMPR(6,6),IBLANK(400)                      05300
      DIMENSION IARRAY(8),ITEXT(200)                                    05400
C                                                                       05500
      COMMON/BLKLAM/IELMST,ILAM1,ILAMX                                  05600
      COMMON/BLKRAT/MEMDEL                                              05700
      COMMON/BLKELM/ELEMTR/BLKSPD/RTRANS,MEMPR                          05800
      COMMON/BLKMEM/MEMFCN/BLKS/ISX                                     05900
      COMMON/BLKTRN/LTRMAP,IALPH,IBLANK                                 06000
      COMMON/BLKTXT/ITEXT                                               06100
C                                                                       06200
      DATA ISX/1,1,0,0,0,0/                                             06300
      DATA MEMFCN/9,11,13,15,9,11,13,15,9,0,11,0,13,0,15,0,             06400
     2  384*0,                                                          06500
     2  10,12,14,16,10,12,14,16,0,10,0,12,0,14,0,16,384*0,              06600
     2  1,0,0,0,5,0,0,0,1,5,1,5,1,5,1,5,384*0,                          06700
     2  0,2,0,0,0,6,0,0,2,6,2,6,2,6,2,6,384*0,                          06800
     2  0,0,3,0,0,0,7,0,3,7,3,7,3,7,3,7,384*0,                          06900
     2  0,0,0,4,0,0,0,8,4,8,4,8,4,8,4,8,384*0/                          07000
C                                                                       07100
      DATA IELMST/1,2,3,4,5,6,7,8,9,10,11,12,                           07200
     2  13,14,15,16,384*0/                                              07300
      DATA ILAM1/3,4,5,6,3,4,5,6,1,2,1,2,1,2,1,2/                       07400
      DATA ILAMX/1,1,0,0,0,0/                                           07500
C                                                                       07600
      DATA LTRMAP/3,4,5,6,3,4,5,6,1,2,1,2,1,2,1,2,384*0/                07700
      DATA IALPH/'A','B','C','D','E','F','G','H','I',                   07800
     2  'J','K','L','M','N','O','P','Q','R','S','T','U',                07900
     2  'V','W','X','Y','Z','1','2','3','4','5','6','7',                08000
     2  '8','9','0',';',':','%','&',0,0,'X','.','AS','SN',              08100
     2  0,0,0,0,'NR','NO','GA','OK','AR','SK',0,0,0,0,                  08200
     2  'IMI',0,0,0,0,'BT',0,0,0,'EEE'/                                 08300
      DATA IBLANK/400*0/                                                08400
C                                                                       08500
C                                                                       08600
      DATA ELEMTR/.55,.5,.5,.5,.55,.5,.5,.5,8*0.,                       08700
     2  .45,.5,.5,.5,.45,.5,.5,.5,8*0.,                                 08800
     2  8*0.,.581,.54,.923,.923,.923,.923,.95,.95,                      08900
     2  8*0.,.335,.376,.062,.062,.062,.062,.04,.04,                     09000
     2  8*0.,.067,.069,.012,.012,.012,.012,.009,.009,                   09100
     2  8*0.,.015,.015,.003,.003,.003,.003,.001,.001/                   09200
C                                                                       09300
C                                                                       09400
      DATA RTRANS/.1,.2,.4,.2,.1,.15,.2,.3,.2,.15/                      09500
      DATA MEMDEL/0,0,2,2,5,10,0,0,2,2,5,10,                            09600
     2  2,2,0,0,0,0,2,2,0,0,0,0,2,2,0,0,0,0,                            09700
     2  2,2,0,0,0,0/                                                    09800
      DATA MEMPR/0,0,1,2,1,2,0,0,1,2,1,2,1,1,0,0,0,0,                   09900
     2  1,1,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0/                            10000
C                                                                       10100
C                                                                       10200
      OPEN(UNIT=20,FILE='MORSEM')                                       10300
      DO 10 I=1,300                                                     10400
      READ(20,30) (IARRAY(K),K=1,8)                                     10500
30    FORMAT(8I3)                                                       10600
      DO 11 K=1,6                                                       10700
11    MEMFCN(I,K)=IARRAY(K+2)                                           10800
      LTRMAP(I)=IARRAY(1)                                               10900
      IELMST(I)=IARRAY(2)                                               11000
      IF((IELMST(I).EQ.7).OR.(IELMST(I).EQ.3))                          11100
     2  IBLANK(I)=1                                                     11200
      IF((IELMST(I).EQ.8).OR.(IELMST(I).EQ.4))                          11300
     2  IBLANK(I)=2                                                     11400
10    CONTINUE                                                          11500
C                                                                       11600
      ENDFILE 20                                                        11700
      OPEN(UNIT=20,FILE='OUTPUT')                                       11800
      DO 50 I=1,300                                                     11900
      WRITE(20,40) (MEMFCN(I,K),K=1,6)                                  12000
C                                                                       12100
40    FORMAT(10X,6(I3,2X))                                              12200
C                                                                       12300
50    CONTINUE                                                          12400
      ENDFILE 20                                                        12500
C                                                                       12600
      OPEN(UNIT=20,FILE='TEXT')                                         12700
      DO 60 I=1,105                                                     12800
      READ(20,70) ITEXT(I)                                              12900
70    FORMAT(I2)                                                        13000
60    CONTINUE                                                          13100
      ENDFILE 20                                                        13200
C                                                                       13300
      RETURN                                                            13400
      END                                                               13500
      SUBROUTINE SIMSG1(X,SIG)                                          00100
C                                                                       00200
      COMMON/BLK1/TAU                                                   00300
      COMMON/BLK2/WC,WCHIRP,ASIGMA,BSIGMA,PHISGM,                       00400
     2RSIGM,TCHIRP,GAMMA                                                00500
      DATA XLAST/1./,BETA/1./                                           00600
      DATA AMP/1./,BFADE/0./,THETA/0./,PHI/0./                          00700
C                                                                       00800
      DUR=BETA                                                          00900
      CALL KEY(DUR,X)                                                   01000
      BETA=BETA*(1.-X-XLAST+2.*X*XLAST)+1.                              01100
      TK=X*(1.-XLAST)                                                   01200
      XLAST=X                                                           01300
C                                                                       01400
      CALL RANDN(W,1,0.,ASIGMA)                                         01500
      AMP=AMP+TK*W                                                      01600
      IF(AMP.LT..01) AMP=.01                                            01700
C                                                                       01800
      CALL RANDN(W,1,0.,BSIGMA)                                         01900
      BFADE=GAMMA*BFADE+W                                               02000
C                                                                       02100
      AMPB=AMP+BFADE                                                    02200
      IF(AMPB.LT.0.001) BFADE=0.001-AMP                                 02300
      AMPB=AMP+BFADE                                                    02400
      TDUR=1000.*TAU*BETA                                               02500
      WCHRP=X*WCHIRP*EXP(-TDUR/TCHIRP)                                  02600
      THETA=THETA+(WC+WCHRP)*TAU                                        02700
      THETA=AMOD(THETA,6.28319)                                         02800
C                                                                       02900
      CALL RANDN(W,1,0.,PHISGM)                                         03000
      PHI=PHI+TK*W                                                      03100
      PHI=AMOD(PHI,6.28319)                                             03200
C                                                                       03300
      SIG=X*AMPB*SIN(THETA+PHI)                                         03400
C                                                                       03500
      CALL RANDN(ZN,1,0.,RSIGM)                                         03600
      SIG=SIG+ZN                                                        03700
C                                                                       03800
C                                                                       03900
C                                                                       04000
      RETURN                                                            04100
      END                                                               04200
C                                                                       04300
      SUBROUTINE KEY(DUR,X)                                             04400
      DIMENSION ESEP(6),EDEV(6),MORSE(10,40)                            04500
      DIMENSION IOUT(500),ISYMBL(6),ITEXT(200)                          04600
      COMMON/BLKEND/IEND                                                04700
      COMMON/BLK1/TAU/BLK6/DMEAN,XDUR,ESEP,EDEV                         04800
      COMMON/BLKTXT/ITEXT                                               04900
C KZ4AP: changes 4 and 9: the trace level and the duration law.
      COMMON/KZ4AP/RATE2,KZTRC,KZLAW,NSTOP,NSW,KZSTP
C KZ4AP: change 2: DATA IK/"001000000000/ (05000) removed with RAN.
C                                                                       05000
      DATA LTR/20/,NELM/0/,N/0/,NLTR/1/                                 05100
      DATA MORSE/1,3,2,0,0,0,0,0,0,0,                                   05200
     2  2,3,1,3,1,3,1,0,0,0,2,3,1,3,1,3,1,0,0,0,                        05300
     2  2,3,1,3,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,                        05400
     2  1,3,1,3,2,3,1,0,0,0,2,3,2,3,1,0,0,0,0,0,                        05500
     2  1,3,1,3,1,3,1,0,0,0,1,3,1,0,0,0,0,0,0,0,                        05600
     2  1,3,2,3,2,3,2,0,0,0,2,3,1,3,2,0,0,0,0,0,                        05700
     2  1,3,2,3,1,3,1,0,0,0,2,3,2,0,0,0,0,0,0,0,                        05800
     2  2,3,1,0,0,0,0,0,0,0,2,3,2,3,2,0,0,0,0,0,                        05900
     2  1,3,2,3,2,3,1,0,0,0,2,3,2,3,1,3,2,0,0,0,                        06000
     2  1,3,2,3,1,0,0,0,0,0,1,3,1,3,1,0,0,0,0,0,                        06100
     2  2,0,0,0,0,0,0,0,0,0,1,3,1,3,2,0,0,0,0,0,                        06200
     2  1,3,1,3,1,3,2,0,0,0,1,3,2,3,2,0,0,0,0,0,                        06300
     2  2,3,1,3,1,3,2,0,0,0,2,3,1,3,2,3,2,0,0,0,                        06400
     2  2,3,2,3,1,3,1,0,0,0,1,3,2,3,2,3,2,3,2,0,                        06500
     2  1,3,1,3,2,3,2,3,2,0,1,3,1,3,1,3,2,3,2,0,                        06600
     2  1,3,1,3,1,3,1,3,2,0,1,3,1,3,1,3,1,3,1,0,                        06700
     2  2,3,1,3,1,3,1,3,1,0,2,3,2,3,1,3,1,3,1,0,                        06800
     2  2,3,2,3,2,3,1,3,1,0,2,3,2,3,2,3,2,3,1,0,                        06900
     2  2,3,2,3,2,3,2,3,2,0,40*0/                                       07000
      DATA ISYMBL/1H.,1H_,1H ,1H/,1H;,1H\/                              07100
C                                                                       07200
C KZ4AP: change 4: KZN counts KEY's calls, one per 8 kHz sample.
      KZN=KZN+1
      BETA=1000.*TAU*DUR                                                07300
      IF(BETA.LT.XDUR) GO TO 200                                        07400
      NELM=NELM+1                                                       07500
      IELM=MORSE(NELM,LTR)                                              07600
      IF(IELM.NE.0) GO TO 100                                           07700
      NELM=0                                                            07800
C KZ4AP: change 2: Y=RAN(IK)
      CALL RANDOM_NUMBER(Y)                                             07900
      IELM=4                                                            08000
      IF(Y.GT..9) IELM=5                                                08100
      IF((Y.LE..9).AND.(Y.GT..8)) IELM=6                                08200
C KZ4AP: change 2: Y=RAN(IK)
      CALL RANDOM_NUMBER(Y)                                             08300
      Y=35*(Y-.001)+1.                                                  08400
      IY=Y                                                              08500
      LTR=IY+1                                                          08600
C                                                                       08700
      GO TO 100                                                         08800
      NLTR=NLTR+1                                                       08900
      LTR=ITEXT(NLTR)                                                   09000
      IF(LTR.EQ.0) IELM=4                                               09100
      IF(LTR.EQ.37) IELM=5                                              09200
      IF(LTR.EQ.38) IELM=6                                              09300
      NLTR=NLTR+1                                                       09400
      LTR=ITEXT(NLTR)                                                   09500
C                                                                       09600
100   N=N+1                                                             09700
      IOUT(N)=ISYMBL(IELM)                                              09800
C KZ4AP: change 4: each element keyed: its first sample (8 kHz),
C KZ4AP: element 1..6, index in the letter (0: letter-ending space)
C KZ4AP: and the letter column (for a space, the next letter's).
      IF(KZTRC.GE.1) WRITE(22,9097) KZN,IELM,NELM,LTR
9097  FORMAT('E ',I10,1X,I1,1X,I2,1X,I2)
      IF(N.LT.300) GO TO 110                                            09900
      N=0                                                               10000
      NLTR=0                                                            10100
      IEND=1                                                            10200
C KZ4AP: change 1: TYPE 900
      PRINT 900                                                         10300
900   FORMAT(/,/,1X,'> END OF RUN; INPUT DATA WAS:',/)                  10400
      DO 10 K=1,10                                                      10500
      K1=(K-1)*50+1                                                     10600
      K2=K*50                                                           10700
C KZ4AP: change 1: TYPE 1000,(IOUT(L),L=K1,K2)
      PRINT 1000,(IOUT(L),L=K1,K2)                                      10800
1000  FORMAT(/,1X,50A1)                                                 10900
10    CONTINUE                                                          11000
C KZ4AP: change 1: the operator's wait ACCEPT 1000,WAIT removed.
C                                                                       11100
C                                                                       11200
110   XM=ESEP(IELM)*DMEAN                                               11300
      XSIGM=EDEV(IELM)*DMEAN                                            11400
C KZ4AP: change 9: LAW 1 (reading C of decision D9) draws the
C KZ4AP: duration from the text's Gaussian (p. 125), mean XM and
C KZ4AP: standard deviation XSIGM (EDEV = sigma in dots), truncated
C KZ4AP: by redrawing below 16 ms and, for dot, dash, e-sp and c-sp,
C KZ4AP: above 360 ms. LAW 0 is the printed uniform law.
      IF(KZLAW.EQ.1) GO TO 9110
C KZ4AP: change 2: Y=RAN(IK)
      CALL RANDOM_NUMBER(Y)                                             11500
      Y=2.*(Y-.5)                                                       11600
      XDUR=XM+Y*XSIGM                                                   11700
      IF(XDUR.LT.20.) XDUR=20.                                          11800
      GO TO 9120
9110  CALL RANDN(W,1,0.,1.)
      XDUR=XM+W*XSIGM
      IF(XDUR.LT.16.) GO TO 9110
      IF((IELM.LE.4).AND.(XDUR.GT.360.)) GO TO 9110
9120  CONTINUE
      X=1.                                                              11900
      IF(IELM.GE.3) X=0.                                                12000
C                                                                       12100
200   RETURN                                                            12200
      END                                                               12300
      SUBROUTINE DISPLA(S1,S2,S3,S4)                                    00100
      DIMENSION S1(512),S2(512),S3(512),S4(512)                         00200
      CALL ERASE                                                        00300
      CALL PLOTR(S1,512,0,XM,400)                                       00400
      CALL PLOTR(S2,512,0,XM,275)                                       00500
      CALL PLOTR(S3,512,1,1.,150)                                       00600
      CALL PLOTR(S4,512,0,XM,40)                                        00700
      CALL VIEW('1')                                                    00800
C KZ4AP: change 1: the operator's wait ACCEPT 1000,WAIT removed.
C                                                                       00900
1000  FORMAT(A5)                                                        01000
      RETURN                                                            01100
      END                                                               01200
C                                                                       01300
C                                                                       01400
C                                                                       01500
C                                                                       01600
      SUBROUTINE STATS(XIN1,XIN2,XIN3,XIN4,S1,                          01700
     2 S2,S3,S4,N)                                                      01800
C                                                                       01900
      DIMENSION S1(512),S2(512),S3(512),S4(512)                         02000
C                                                                       02100
      S1(N)=XIN1                                                        02200
      S2(N)=XIN2                                                        02300
      S3(N)=XIN3                                                        02400
      S4(N)=XIN4                                                        02500
C                                                                       02600
      RETURN                                                            02700
      END                                                               02800
C                                                                       02900
C                                                                       03000
C                                                                       03100
      SUBROUTINE AUTOCR(S5,RS)                                          03200
C                                                                       03300
      DIMENSION S5(512),RS(512),S(1000),RS1(500)                        03400
C                                                                       03500
      DATA S/1000*0./,XN/0./                                            03600
C                                                                       03700
C                                                                       03800
      XN=XN+1                                                           03900
      DO 100 I=1,500                                                    04000
      S(I)=S5(I)                                                        04100
      RS1(I)=0.                                                         04200
100   CONTINUE                                                          04300
C                                                                       04400
      DO 200 I=1,500                                                    04500
      DO 300 K=1,500                                                    04600
      RS1(I)=RS1(I)+S(K+I-1)*S(K)                                       04700
300   CONTINUE                                                          04800
200   CONTINUE                                                          04900
C                                                                       05000
      DO 400 I=1,500                                                    05100
      RS(I)=(RS(I)*(XN-1.)+RS1(I))/XN                                   05200
400   CONTINUE                                                          05300
C                                                                       05400
      RETURN                                                            05500
      END                                                               05600
      SUBROUTINE PROCES(Z,RN,XHAT,PX,ELMHAT,LTRHAT)                     00100
C                                                                       00200
C*************************************************************          00300
C                                                                       00400
C   THIS SUBROUTINE IMPLEMENTS THE PROCESSING ALGORITHM                 00500
C   FOR JOINT DEMODULATION,DECODING,AND TRANSLATION OF                  00600
C   THE RECEIVED MORSE PROCESS. IT TAKES IN A NEW MEASURE-              00700
C   MENT,Z,OF THE DETECTED SIGNAL EVERY 5 MSEC AND PRO-                 00800
C   DUCES AN ESTIMATE OF THE CURRENT KEYSTATE,ELEMENT                   00900
C   STATE,AND LETTER OF THE RECEIVED SIGNAL.                            01000
C                                                                       01100
C   DEFINITIONS OF VARIABLE NAMES:                                      01200
C       Z-        INPUT SAMPLE OF DETECTED SIGNAL                       01300
C       RN-       INPUT NOISE POWER ESTIMATE                            01400
C       XHAT-     OUTPUT ESTIMATE OF KEYSTATE                           01500
C       ELMHAT-   OUTPUT ESTIMATE OF ELEMENT STATE                      01600
C       LTRHAT-   OUTPUT ESTIMATE OF LETTER STATE                       01700
C                                                                       01800
C       ISAVE-    NO. OF PREVIOUS PATHS SAVED                           01900
C       IPATH-    IDENTITY OF SAVED PATH                                02000
C       LAMBDA(I)-IDENTITY OF LTR STATE OF SAVED PATH I                 02100
C       DUR(I)-   DURATION OF ELEMENT ON PATH I                         02200
C       ILRATE(I)-IDENTITY OF DATA RATE ON PATH I                       02300
C       PIN(I,N)- COMPUTED TRANS PROB FROM PATH I TO STATE N            02400
C       LAMSAV(J)-IDENTITY OF LTR STATE AT NEW NODE J                   02500
C       ILRSAV(J)-IDENTITY OF DATA RATE AT NEW NODE J                   02600
C       LKHD(J)-  LIKELIHOOD VALUE FOR NODE J                           02700
C       P(J)-     COMPUTED POSTERIOR PROB OF PATH                       02900
C                 ENDING AT NEW NODE J                                  02900
C       PSELEM(K)-COMPUTED POSTERIOR PROB OF ELEM K                     03000
C       SPDHAT   -COND MEAN ESTIMATE OF INSTANT DATA RATE               03100
C       PX-       POSTERIOR PROB THAT KEYSTATE EQUALS 1                 03200
C                                                                       03300
C   THE FOLLOWING SUBROUTINES ARE UTILIZED:                             03400
C       TRPROB- COMPUTES TRANSITION PROBABILITIES                       03500
C       PATH-   COMPUTES IDENTITY OF NEW PATHS                          03600
C       LIKHD-  COMPUTES THE LIKELIHOOD OF EACH PATH EXTENSION          03700
C       PROBP-  COMPUTES POSTERIOR PROBS OF EACH NEW PATH               03800
C       SPROB-  COMPUTES POSTERIOR PROBS OF EACH STATE                  03900
C       SAVE-   SAVES THE HIGHEST PROB PATHS                            04000
C       TRELIS- FORMS A TRELIS OF SAVED PATHS                           04100
C       TRANSL- TRANSLATES THE LETTER ESTIMATE                          04200
C                                                                       04300
C   ALL TABLES OF CONSTANTS ARE STORED IN COMMON.                       04400
C                                                                       04500
C**************************************************************         04600
C                                                                       04700
      REAL LKHD                                                         04800
      INTEGER ELMHAT,XHAT,PATHSV,SORT                                   04900
      DIMENSION LAMBDA(25),DUR(25),ILRATE(25),PIN(25,30)                05000
      DIMENSION LAMSAV( 750),DURSAV( 750),ILRSAV( 750)                  05100
      DIMENSION LKHD(750),P(750),PSELEM(6)                              05200
      DIMENSION PATHSV(25),SORT(25)                                     05300
C KZ4AP: change 4: the trace level.
      COMMON/KZ4AP/RATE2,KZTRC,KZLAW,NSTOP,NSW,KZSTP
C                                                                       05400
      DATA ISAVE/25/                                                    05500
      DATA LAMBDA/25*5/                                                 05600
      DATA ILRATE/5*10,5*20,5*30,5*40,5*50/                             05700
      DATA P/750*1./                                                    05800
C                                                                       05900
      DATA LAMSAV/750*5/,DUR/25*1000./                                  06000
      DATA ILRSAV/750*20/,PATHSV/25*5/                                  06100
C                                                                       06200
C                                                                       06300
C   FOR EACH SAVED PATH, COMPUTE:                                       06400
C     TRANSITION PROBABILITY TO NEW STATE (TRPROB);                     06500
C     IDENTITY OF EACH NEW PATH EXTENDED (PATH);                        06600
C     LIKELIHOOD OF EACH STATE EXTENSION (LIKHD):                       06700
C                                                                       06800
C                                                                       06900
      DO 100 I=1,ISAVE                                                  07000
      IPATH=I                                                           07100
C                                                                       07200
      CALL TRPROB(IPATH,LAMBDA(I),DUR(I),ILRATE(I),PIN)                 07300
C KZ4AP: line 07400 is cut off in the reproduced printout after
C KZ4AP: "...,DURSAV,ILR"; completed as ILRSAV), the seventh argument
C KZ4AP: of PATH (reference sheet (b), PROCES). Not a change.
      CALL PATH(IPATH,LAMBDA(I),DUR(I),ILRATE(I),LAMSAV,DURSAV,ILRSAV)  07400
      CALL LIKHD(Z,RN,IPATH,LAMBDA(I),DUR(I),                           07500
     2  ILRATE(I),PIN,LKHD)                                             07600
C                                                                       07700
100   CONTINUE                                                          07800
C                                                                       07900
C   HAVING OBTAINED ALL NEW PATHS, COMPUTE:                             08000
C     POSTERIOR PROBABILITY OF EACH NEW PATH (PROBP);                   08100
C     POSTERIOR PROBABILITY OF KEYSATE,ELEM STATE,                      08200
C       CONDITIONAL MEAN ESTIMATE OF SPEED (SPROB);                     08300
C                                                                       08400
C                                                                       08500
      CALL PROBP(P,PIN,ISAVE,LKHD)                                      08600
      CALL SPROB(P,ISAVE,ILRSAV,PELM,KHAT,                              08700
     2  SPDHAT,PX)                                                      08800
C                                                                       06900
      XHAT=0                                                            09000
      IF(PX.GT.0.5) XHAT=1                                              09100
C                                                                       09200
C   SAVE THE PATHS WITH HIGHEST PROBABILITY,AND                         09300
C   STORE THE VALUES CORRESPONDING TO THESE PATHS:                      09400
C                                                                       09500
      CALL SAVEP(P,PATHSV,ISAVE,IMAX,LAMSAV,DURSAV,                     09600
     2  ILRSAV,LAMBDA,DUR,ILRATE,SORT)                                  09700
C KZ4AP: change 7: GO TO 1 jumped to the terminal statement of DO 1
C KZ4AP: from outside the loop; redirected to 9001 after the loop.
      GO TO 9001                                                        09750
C KZ4AP: change 1: TYPE 1000,Z
      PRINT 1000,Z                                                      09800
1000  FORMAT(//,4X,F10.7,/)                                             09900
      DO 1 IN=1,ISAVE                                                   10000
C KZ4AP: change 1: TYPE 1100, ...
      PRINT 1100, IN,P(IN),PATHSV(IN),LAMBDA(IN),DUR(IN),ILRATE(IN)     10100
     2  ,LKHD(SORT(IN))                                                 10200
1100  FORMAT(1X,I3,2X,F10.7,2X,I3,2X,I3,2X,F6.1, 2X,I3,2X,F10.7)        10300
1     CONTINUE                                                          10400
9001  CONTINUE
C                                                                       10500
C                                                                       10600
C   UPDATE TRELLIS WITH NEW SAVED NODES,AND                             10700
C   OBTAIN LETTER STATE ESTIMATE:                                       10800
C                                                                       10900
      CALL TRELIS(ISAVE,PATHSV,LAMBDA,IMAX)                             11000
C KZ4AP: change 4: trace of the saved paths after TRELIS.
      IF(KZTRC.GE.2) CALL KZSTEP(Z,RN,ISAVE,IMAX,PX,XHAT,SPDHAT,
     2  LAMBDA,DUR,ILRATE,PATHSV,P)
C                                                                       11100
200   RETURN                                                            11200
      END                                                               11300
C                                                                       11400
C                                                                       11500
C                                                                       11600
C                                                                       11700
C                                                                       11800
C                                                                       11900
C                                                                       12000
C                                                                       12100
C                                                                       12200
      SUBROUTINE TRPROB(IP,LAMBDA,DUR,ILRATE,P)                         12300
C                                                                       12400
C****************************************************************       12500
C                                                                       12600
C   THIS SUBROUTINE COMPUTES THE TRANSITION PROBABILITY                 12700
C   FROM SAVED PATH IP TO EACH STATE N AND STORES THE                   12800
C   RESULT IN P(IP,N).                                                  12900
C                                                                       13000
C   VARIABLES:                                                          13100
C       IP-     INPUT SAVED PATH IDENTITY                               13200
C       LAMBDA- INPUT SAVED LTR STATE IDENTITY                          13300
C       DUR-    INPUT SAVED ELEMENT DURATION                            13400
C       ILRATE- INPUT SAVED DATA RATE IDENTITY                          13500
C        P-      OUTPUT TRANSITION PROBABILITY MATRIX                   13600
C                                                                       13700
C   THE FOLLOWING FUNCTION SUBROUTINES ARE USED:                        13800
C       XTRANS- RETURNS THE KEYSTATE TRANSITION PROBABILITY             13900
C               CONDITIONED ON ELEMENT TYPE AND DATA RATE               14000
C       PTRANS- RETURNS THE PATH-CONDITIONAL STATE TRANSITION PROB      14100
C                                                                       14200
C                                                                       14300
C****************************************************************       14400
C                                                                       14500
      DIMENSION P(25,30),IELMST(400),ILAM1(16),ILAMX(6)                 14600
      DIMENSION PIN(30)                                                 14700
C                                                                       14800
      COMMON /BLKLAM/IELMST,ILAM1,ILAMX                                 14900
C                                                                       15000
C                                                                       15100
C   LOOK UP ELEMENT TYPE FOR LTR STATE LAMBDA:                          15200
C                                                                       15300
      IF(LAMBDA.NE.0) GO TO 20                                          15400
      DO 10 N=1,30                                                      15500
      P(IP,N)=0.                                                        15600
10    CONTINUE                                                          15700
      GO TO 200                                                         15800
C                                                                       15900
C                                                                       16000
20    IELEM=ILAM1(IELMST(LAMBDA))                                       16100
C                                                                       16200
C   COMPUTE KEYSTATE TRANSITION PROBABILITY:                            16300
C                                                                       16400
      PTRX=XTRANS(IELEM,DUR,ILRATE)                                     16500
C                                                                       16600
C   FOR EACH STATE, COMPUTE STATE TRANSITION PROBABILITY:               16700
C                                                                       16800
      PSUM=0.                                                           16900
      DO 100 K=1,6                                                      17000
      DO 100 I=1,5                                                      17100
      N=(I-1)*6+K                                                       17200
      KELM=K                                                            17300
      IRATE=I                                                           17400
      CALL PTRANS(KELM,IRATE,LAMBDA,ILRATE,PTRX,PSUM,PIN,N)             17500
100   CONTINUE                                                          17600
C                                                                       17700
      DO 300 N=1,30                                                     17800
      P(IP,N)=PIN(N)/PSUM                                               17900
300   CONTINUE                                                          18000
C                                                                       18100
200   RETURN                                                            18200
      END                                                               18300
C                                                                       18400
C                                                                       18500
      FUNCTION XTRANS(IELEM,D0,IRATE)                                   18600
C                                                                       18700
C****************************************************************       18800
C                                                                       18900
C   THIS FUNCTION IMPLEMENTS THE CALCULATION OF KEYSTATE                19000
C   TRANSITION PROBABILITY, CONDITIONED ON ELEMENT TYPE,                19100
C   CURRENT DURATION, AND DATA RATE.                                    19200
C                                                                       19300
C   VARIABLES:                                                          19400
C       IELEM- INPUT CURRENT ELEMENT TYPE                               19500
C       D0-    INPUT CURRENT ELEMENT DURATION                           19600
C       IRATE- INPUT CURRENT DATA RATE                                  19700
C                                                                       19800
C   TABLES IN COMMON CONTAIN DENSITY PARMS FOR EACH                     19900
C   ELEMENT TYPE, DATA RATE.                                            20000
C                                                                       20100
C****************************************************************       20200
C                                                                       20300
      DIMENSION KIMAP(6),APARM(3)                                       20400
      DATA KIMAP/1,3,1,3,7,14/                                          20500
      DATA APARM/3.000,1.500,1.000/                                     20600
C                                                                       20700
C                                                                       20800
C   SCALE DURATION AND OBTAIN DENSITY PARAMETER:                        20900
C                                                                       21000
      MSCALE=KIMAP(IELEM)                                               21100
      RSCALE=1200./IRATE                                                21200
      B0=D0/(MSCALE*RSCALE)                                             21300
      B1=(D0+5.)/(MSCALE*RSCALE)                                        21400
C                                                                       21500
      IF(IELEM.EQ.6) GO TO 20                                           21600
      IF(IELEM.EQ.5) GO TO 10                                           21700
      ALPHA=MSCALE*APARM(1)                                             21800
      GO TO 100                                                         21900
C                                                                       22000
10    ALPHA=7.*APARM(2)                                                 22100
      GO TO 100                                                         22200
C                                                                       22300
20    ALPHA=14.*APARM(3)                                                22400
C                                                                       22500
100   IF(B1.LE.1.) GO TO 200                                            22600
      IF((B0.LT.1.).AND.(B1.GT.1.)) GO TO 300                           22700
      XTRANS=EXP(-ALPHA*(B1-B0))                                        22800
      GO TO 400                                                         22900
C                                                                       23000
200   P1=1.-0.5*EXP(ALPHA*(B1-1.))                                      23100
      P0=1.-0.5*EXP(ALPHA*(B0-1.))                                      23200
      XTRANS=P1/P0                                                      23300
      GO TO 400                                                         23400
C                                                                       23500
300   P1=0.5*EXP(-ALPHA*(B1-1.))                                        23600
      P0=1.-0.5*EXP(ALPHA*(B0-1.))                                      23700
      XTRANS=P1/P0                                                      23800
C                                                                       23900
400   RETURN                                                            24000
      END                                                               24100
C                                                                       24200
C                                                                       24300
C                                                                       24400
C                                                                       24500
      SUBROUTINE PTRANS(KELEM,IRATE,LAMBDA,ILRATE,PTRX,                 24600
     2  PSUM,PIN,N)                                                     24700
C                                                                       24800
C**********************************************************             24900
C                                                                       25000
C   THIS FUNCTION SUBROUTINE RETURNS THE PATH CONDITIONAL               25100
C   TRANSITION PROBABILITIES TO EACH ALLOWABLE STATE N.                 25200
C                                                                       25300
C   VARIABLES:                                                          25400
C       KELEM-  INPUT CURRENT ELEMENT STATE                             25500
C       IRATE-  INPUT CURRENT DATA RATE STATE                           25600
C       LAMBDA- INPUT IDENTITY OF CURRENT LTR STATE                     25700
C       PTRX-   INPUT KEYSTATE TRANSITION PROBABILITY                   25800
C       ELEMTR- ELEMENT TRANSITION PROBABILITY MATRIX                   25900
C                                                                       26000
C   FUNCTION SUBROUTINE USED:                                           26100
C       SPDTR-  RETURNS DATA RATE TANSITION PROBS,                      26200
C               CONDITIONED ON CURRENT SPACE TYPE.                      26300
C                                                                       26400
C**********************************************************             26500
C                                                                       26600
      DIMENSION IELMST(400),ILAM1(16),ELEMTR(16,6)                      26700
      DIMENSION ILAMX(6),PIN(30)                                        26800
C                                                                       26900
      COMMON/BLKLAM/IELMST,ILAM1,ILAMX                                  27000
      COMMON/BLKELM/ELEMTR                                              27100
C                                                                       27200
C   IF THE SAVED ELEMENT AND THE ELEMENT OF THE STATE                   27300
C    N TO WHICH THE PATH IS BEING EXTENDED ARE THE                      27400
C    SAME, THEN THE STATE TRANS PROB IS SIMPLY                          27500
C   KEYSTATE TRANS PROB:                                                27600
C                                                                       27700
      IF(KELEM.NE.ILAM1(IELMST(LAMBDA))) GO TO 100                      27800
      PIN(N)=PTRX                                                       27900
      IF(IRATE.NE.3) PIN(N)=0.                                          28000
      GO TO 200                                                         28100
C                                                                       28200
C                                                                       28300
C   OTHERWISE:                                                          28400
C                                                                       28500
C    OBTAIN ELEM TRANS PROBS FROM TABLE:                                28600
C                                                                       28700
100   PELEM=ELEMTR(IELMST(LAMBDA),KELEM)                                28800
C                                                                       28900
C                                                                       29000
C   NEXT COMPUTE  ELEM-CONDITIONAL SPEED TRANS PROB:                    29100
C                                                                       29200
      PRATE=SPDTR(IRATE,ILRATE,KELEM,ILAM1(IELMST(LAMBDA)))             29300
C                                                                       29400
C                                                                       29500
C   PTRANS IS THE PRODUCT:                                              29600
C                                                                       29700
      PIN(N)=(1.-PTRX)*PELEM*PRATE                                      29800
200   PSUM=PSUM+PIN(N)                                                  29900
C                                                                       30000
      RETURN                                                            30100
      END                                                               30200
C                                                                       30300
C                                                                       30400
C                                                                       30500
C                                                                       30600
C                                                                       30700
      FUNCTION SPDTR(ISRT,ILRT,ISELM,ILELM)                             30800
C                                                                       30900
C*************************************************************          31000
C                                                                       31100
C   THIS FUNCTION RETURNS THE DATA RATE (SPEED) TRANSITION              31200
C   PROBABILITY BASED ON THE CURRENT ELEM  TYPE. THE  ALLOW-            31300
C   ABLE TRANSITION PROBS ARE STORED IN THE TABLE RTRANS.               31400
C                                                                       31500
C    VARIABLES:                                                         31600
C       ISRT-   DATA RATE IDENTITY FOR STATE TO WHICH                   31700
C               PATH IS BEING EXTENDED                                  31800
C       ILRT-   DATA RATE ON CURRENT PATH                               31900
C       ISELM-  ELEM TYPE FOR NEXT STATE                                32000
C       ILELM-  ELEM TYPE ON CURRENT PATH                               32100
C                                                                       32200
C*************************************************************          32300
C                                                                       32400
      DIMENSION RTRANS(5,2),MEMPR(6,6),MEMDEL(6,6)                      32500
      COMMON/BLKSPD/RTRANS,MEMPR                                        32600
      COMMON/BLKRAT/MEMDEL                                              32700
C                                                                       32800
C                                                                       32900
C   IF SAVED ELEMENT AND NEW ELEMENT ARE THE                            33000
C   SAME, THEN THERE CAN BE NO SPEED CHANGE:                            33100
C                                                                       33200
      IF(ILELM.NE.ISELM) GO TO 100                                      33300
      SPDTR=1.                                                          33400
      IF(ISRT.NE.3) SPDTR=0.                                            33500
      GO TO 300                                                         33600
C                                                                       33700
C                                                                       33800
C   OTHERWISE, OBTAIN SPEED TRANSITION PROB:                            33900
C                                                                       34000
C                                                                       34100
100   IDEL=MEMDEL(ILELM,ISELM)                                          34200
      IND1=MEMPR(ILELM,ISELM)                                           34300
      IF(IND1.NE.0) GO TO 200                                           34400
      SPDTR=0.                                                          34500
      GO TO 300                                                         34600
C                                                                       34700
200   IDELSP=(ISRT-3)*IDEL                                              34800
      SPDTR=RTRANS(ISRT,IND1)                                           34900
      ISRATE=ILRT+IDELSP                                                35000
      IF(ISRATE.GT.60) SPDTR=0.                                         35100
      IF(ISRATE.LT.10) SPDTR=0.                                         35200
C                                                                       35300
300   RETURN                                                            35400
      END                                                               35500
C                                                                       35600
C                                                                       35700
C                                                                       35800
C                                                                       35900
C                                                                       36000
C                                                                       36100
C                                                                       36200
C                                                                       36300
      SUBROUTINE PATH(IP,LAMBDA,DUR,ILRATE,LAMSAV,DURSAV,ILRSAV)        36400
C                                                                       36500
C******************************************************************     36600
C                                                                       36700
C   PATH COMPUTES THE LTR STATE, DURATION, AND DATA RATE OF             36800
C   EACH NEW PATH EXTENDED TO STATE N.                                  36900
C                                                                       37000
C   VARIABLES:                                                          37100
C       IP-      SAVED PATH IDENTITY                                    37200
C       LAMBDA-  LTR STATE OF SAVED PATH                                37300
C       DUR-     DURATION OF ELEMENT ON SAVED PATH                      37400
C       ILRATE-  DATA RATE OF ELEMENT ON SAVED PATH                     37500
C       LAMSAV-  NEW LTR STATES FOR EACH PATH EXTENSION                 37600
C       DURSAV-  NEW ELEM DURATIONS FOR EACH PATH EXTENSION             37700
C       ILRSAV-  NEW DATA RATES FOR EACH PATH EXTENSION                 37800
C       J-       NEW PATH IDENTITY                                      37900
C                                                                       38000
C   THE LETTER TRANSITION TABLE, MEMFCN, IS STORED IN COMMON.           38100
C                                                                       38200
C******************************************************************     38300
C                                                                       38400
      DIMENSION LAMSAV( 750),DURSAV( 750),ILRSAV( 750)                  38500
      DIMENSION MEMFCN(400,6),IELMST(400),ILAM1(16)                     38600
      DIMENSION ILAMX(6),ISX(6),MEMDEL(6,6)                             38700
C                                                                       38800
      COMMON/BLKLAM/IELMST,ILAM1,ILAMX                                  38900
      COMMON/BLKMEM/MEMFCN                                              39000
      COMMON/BLKS/ISX                                                   39100
      COMMON/BLKRAT/MEMDEL                                              39200
C                                                                       39300
C   FOR EACH ELEM STATE K, AND EACH SPEED I, COMPUTE:                   39400
C                                                                       39500
      DO 100 I=1,5                                                      39600
      DO 100 K=1,6                                                      39700
C                                                                       39800
C    STATE IDENTITY N:                                                  39900
C                                                                       40000
      N=(I-1)*6+K                                                       40100
C                                                                       40200
C    NEW PATH IDENTITY:                                                 40300
C                                                                       40400
      J=(IP-1)*30+N                                                     40500
C                                                                       40600
C    NEW LTR STATE:                                                     40700
C                                                                       40800
      IF(LAMBDA.NE.0) GO TO 50                                          40900
      LAMSAV(J)=0                                                       41000
      GO TO 100                                                         41100
C                                                                       41200
50    LAMSAV(J)=MEMFCN(LAMBDA,K)                                        41300
      IF(LAMSAV(J).EQ.0) GO TO 100                                      41400
C                                                                       41500
C    NEW DURATION:                                                      41600
C                                                                       41700
C     OBTAIN KEYSTATE OF SAVED PATH AND NEW STATE:                      41800
C                                                                       41900
C                                                                       42000
      ILELM=ILAM1(IELMST(LAMBDA))                                       42100
      IXL=ILAMX(ILELM)                                                  42200
      IXS=ISX(K)                                                        42300
C                                                                       42400
C     CALCULATE DURATION:                                               42500
C                                                                       42600
      DURSAV(J)=DUR*(1-IXS-IXL+2*IXS*IXL)+5.                            42700
C                                                                       42800
C    NEW DATA RATE:                                                     42900
C                                                                       43000
      ILRSAV(J)=ILRATE+(I-3)*MEMDEL(ILELM,K)                            43100
C                                                                       43200
100   CONTINUE                                                          43300
C                                                                       43400
200   RETURN                                                            43500
      END                                                               43600
C                                                                       43700
C                                                                       43800
C                                                                       43900
C                                                                       44000
      SUBROUTINE LIKHD(Z,RN,IP,LAMBDA,DUR,                              44100
     2  ILRATE,P,LKHD)                                                  44200
C                                                                       44300
C**************************************************************         44400
C                                                                       44500
C   THIS SUBROUTINE CALCULATES, FOR EACH PATH                           44600
C   EXTENSION TO STATE N, THE LIKELIHOOD OF THAT                        44700
C   TRANSITION GIVEN THE MEASUREMENT Z.  IT USES                        44800
C   AN ARRAY OF LINEAR (KALMAN) FILTERS TO DO SO.                       44900
C                                                                       45000
C   VARIABLES:                                                          45100
C       Z-      INPUT MEASUREMENT                                       45200
C       RN-     INPUT NOISE POWER ESTIMATE                              45300
C       IP-     INPUT SAVED PATH IDENTITY                               45400
C       LAMBDA- INPUT SAVED LTR STATE IDENTITY                          45500
C       DUR-    INPUT SAVED DURATION OF ELEMENT ON PATH IP              45600
C       ILRATE- INPUT SAVED DATA RATE (SPEED)                           45700
C       P-      INPUT TRANSITION PROBABILITIES                          45800
C       LKHD-   OUTPUT COMPUTED LIKELIHOODS FOR EACH TRANS              45900
C                                                                       46000
C   SUBROUTINES USED:                                                   46100
C       KALFIL- KALMAN FILTER FOR EACH NEW PATH                         46200
C                                                                       46300
C**************************************************************         46400
C                                                                       46500
      REAL LKHD,LKHDJ                                                   46600
      DIMENSION P(25,30),LKHD(750)                                      46700
      DIMENSION IELMST(400),ILAM1(16),ILAMX(6)                          46800
      DIMENSION ISX(6)                                                  46900
C                                                                       47000
      COMMON/BLKLAM/IELMST,ILAM1,ILAMX                                  47100
      COMMON/BLKS/ISX                                                   47200
C                                                                       47300
C                                                                       47400
C   OBTAIN SAVED KEYSTATE:                                              47500
C                                                                       47600
C                                                                       47700
C KZ4AP: change 6: for a dead path (LAMBDA = 0) the listing reads
C KZ4AP: IELMST(0), outside the array; every transition probability
C KZ4AP: of such a path is 0, so KALFIL would set each likelihood to
C KZ4AP: 0 and write no Kalman state. The guard does exactly that.
      IF(LAMBDA.NE.0) GO TO 9478
      DO 9477 K=1,6
      DO 9477 I=1,5
      LKHD((IP-1)*30+(I-1)*6+K)=0.
9477  CONTINUE
      GO TO 200
9478  CONTINUE
      KELEM=ILAM1(IELMST(LAMBDA))                                       47800
      ILX=ILAMX(KELEM)                                                  47900
C                                                                       48000
C                                                                       48100
C   FOR EACH STATE:                                                     48200
C                                                                       48300
      DO 100 K=1,6                                                      48400
      DO 100 I=1,5                                                      48500
C                                                                       48600
C   OBTAIN KEYSTATE,RATE STATE,STATE N,NEW NODE:                        48700
C                                                                       48800
      IXS=ISX(K)                                                        48900
      ISRATE=I                                                          49000
      N=(I-1)*6+K                                                       49100
      J=(IP-1)*30+N                                                     49200
      PIN=P(IP,N)                                                       49300
C                                                                       49400
C   COMPUTE AND STORE LIKELIHOOD:                                       49500
C                                                                       49600
      CALL KALFIL(Z,IP,RN,ILX,IXS,KELEM,J,ISRATE,                       49700
     2  DUR,ILRATE,PIN,LKHDJ)                                           49800
C                                                                       49900
      LKHD(J)=LKHDJ                                                     50000
      GO TO 100                                                         50100
      IF(PIN.LE.1.E-06) GO TO 100                                       50200
C KZ4AP: change 1: TYPE 1000,...
      PRINT 1000,IP,Z,LAMBDA,K,ILRATE,ISRATE,DUR,PIN,LKHDJ,RN           50300
1000  FORMAT(1X,I2,1X,F5.3,2X,I3,2X,I1,2X,I2,2X,I2,3X,F5.1,             50400
     2  2X,F8.6,2X,F8.4,2X,F8.4)                                        50500
C                                                                       50600
100   CONTINUE                                                          50700
200   RETURN                                                            50800
      END                                                               50900
C                                                                       51000
C                                                                       51100
C                                                                       51200
      SUBROUTINE KALFIL(Z,IP,RN,ILX,IXS,KELEM,                          00100
     2  JNODE,ISRATE,DUR,ILRATE,PIN,LKHDJ)                              00200
C                                                                       00300
C****************************************************************       00400
C                                                                       00500
C   THIS SUBROUTINE COMPUTES THE ARRAY OF KALMAN FILTER                 00600
C   RECURSIONS USED TO DETERMINE THE LIKELIHOODS.                       00700
C                                                                       00800
C   VARIABLES:                                                          00900
C       Z-      INPUT MEASUREMENT                                       01000
C       IP-     INPUT PATH IDENTITY                                     01100
C       RN-     INPUT NOISE POWER ESTIMATE                              01200
C       ILX-    INPUT SAVED KEYSTATE ON PATH IP                         01300
C       IXS-    INPUT KEYSTAT OF NEW NODE                               01400
C       KELEM-  INPUT ELEM STATE OF NEW NODE                            01500
C       ISRATE- INPUT SPEED STATE OF NEW NODE                           01600
C       DUR-    INPU CURRENT DURATION OF ELEMENT ON IP                  01700
C       ILRATE- INPUT SPEED STATE ON PATH IP                            01800
C       PIN-    TRANS PROB FROM PATH IP TO NODE N                       01900
C       LKHDJ-  OUTPUT CALCULATED LIKELIHOOD VALUE                      02000
C                                                                       02100
C   SUBROUTINES USED                                                    02200
C       MODEL- OBTAINS THE SIGNAL-STATE-DEPENDENT LINEAR                02300
C       MODEL FOR THE KALMAN FILTER RECURSIONS                          02400
C                                                                       02500
C****************************************************************       02600
C                                                                       02700
      REAL LKHDJ                                                        02800
      DIMENSION YKKIP(25),PKKIP(25)                                     02900
      DIMENSION YKKSV(750),PKKSV(750)                                   03000
C                                                                       03100
C                                                                       03200
      COMMON/BLKSV1/YKKIP,PKKIP,YKKSV,PKKSV                             03300
C                                                                       03400
C                                                                       03500
C                                                                       03600
      DATA YKKIP/25*.5/,PKKIP/25*.10/                                   03700
      DATA PINMIN/.00010/                                               03800
C                                                                       03900
C                                                                       04000
C   IF TRANSITION PROBABILITY IS VERY SMALL, DON'T                      04100
C   BOTHER WITH LIKELIHOOD CALCULATION:                                 04200
C                                                                       04300
      IF(PIN.GT.PINMIN) GO TO 100                                       04400
      LKHDJ=0.                                                          04500
      GO TO 400                                                         04600
C                                                                       04700
C                                                                       04800
C   OBTAIN STATE-DEPENDENT MODEL PARAMETERS:                            04900
C                                                                       05000
100   CALL MODEL(DUR,KELEM,ILRATE,ISRATE,IXS,PHI,QA,HZ)                 05100
C                                                                       05200
C   GET PREVIOUS ESTIMATES FOR PATH IP                                  05300
C                                                                       05400
      YKK=YKKIP(IP)                                                     05500
      PKK=PKKIP(IP)                                                     05600
C                                                                       05700
C                                                                       05800
C   IMPLEMENT KALMAN FILTER FOR THIS TRANSITION:                        05900
C                                                                       06000
      YPRED=PHI*YKK                                                     06100
C                                                                       06200
      PPRED=PHI*PKK*PHI+QA                                              06300
      PZ=HZ*PPRED+RN                                                    06400
C                                                                       06500
      PZINV=1./PZ                                                       06600
C                                                                       06700
      G=PPRED*HZ*PZINV                                                  06800
C                                                                       06900
      PEST=(1.-G*HZ)*PPRED                                              07000
C                                                                       07100
      ZR=Z-HZ*YPRED                                                     07200
C                                                                       07300
      YKKSV(JNODE)=YPRED+G*ZR                                           07400
      PKKSV(JNODE)=PEST                                                 07500
      IF(YKKSV(JNODE).LE..01) YKKSV(JNODE)=.01                          07600
C                                                                       07700
      A=0.5*PZINV*ZR**2                                                 07800
      IF(A.LE.1000.) GO TO 200                                          07900
      LKHDJ=0.                                                          08000
      GO TO 400                                                         08100
C                                                                       08200
200   LKHDJ=(1./SQRT(PZ))*EXP(-A)                                       08300
      GO TO 400                                                         08400
C KZ4AP: change 1: TYPE 1000,...
      PRINT 1000,Z,HZ,QA,PHI,PZ,ZR,G,PEST,YKK,YKKSV(JNODE),LKHDJ        08500
1000  FORMAT(1X,11(F6.3,1X),/)                                          08600
400   RETURN                                                            08700
      END                                                               08800
C                                                                       08900
C                                                                       09000
C                                                                       09100
C                                                                       09200
C                                                                       09300
C                                                                       09400
      SUBROUTINE MODEL(DUR,IELM,ILR,ISR,IXS,PHI,QA,HZ)                  09500
C                                                                       09600
C**********************************************************             09700
C                                                                       09800
C   THIS SUBROUTINE COMPUTES THE PARAMETERS OF THE                      09900
C   OBSERVATION STATE TRANSITION MATRIX PHI, THE                        10000
C   MEASUREMENT MATRIX, AND THE COVARIANCES.                            10100
C                                                                       10200
C   VARIABLES:                                                          10300
C       DUR-   INPUT ELEMENT DURATION                                   10400
C       IELM-  INPUT ELEMENT TYPE                                       10500
C       ILR-   INPUT SAVED RATE                                         10600
C       ISR-   INPUT RATE OF NEW STATE                                  10700
C       IXS-   INPUT KEYSTATE OF NEW STATE                              10800
C       PHIA-  OUTPUT STATE TRANSITION MATRIX ENTRY FOR                 10900
C       SIGNAL AMPLITUDE STATE                                          11000
C       QA-    OUTPUT COVARIANCE FOR AMPLITUDE STATE                    11100
C       HZ-    OUTPUT MEASUREMENT MATRIX VALUE                          11200
C                                                                       11300
C***********************************************************            11400
C                                                                       11500
C                                                                       11600
C   COMPUTE MEASUREMENT COEFFICIENT:                                    11700
C                                                                       11800
      HZ=IXS                                                            11900
C                                                                       12000
C                                                                       12100
C   COMPUTE PHI AND AMPLITUDE STATE VARIANCE (Q):                       12200
C                                                                       12300
      R1=1200./ILR                                                      12400
      BAUDS=DUR/R1                                                      12500
      IF(BAUDS.GE.14.) BAUDS=14.                                        12600
C                                                                       12700
      IF(IELM.GE.3) GO TO 100                                           12800
      QA=.0001                                                          12900
      PHI=1.                                                            13000
      GO TO 300                                                         13100
C                                                                       13200
100   IF(IXS.EQ.0) GO TO 200                                            13300
      PHI=1.                                                            13400
      QA=0.15*EXP(0.6*(BAUDS-14.))                                      13500
      QA=QA+.01*BAUDS*EXP(.2*(1.-BAUDS))                                13600
      GO TO 300                                                         13700
C                                                                       13800
200   XSAMP=22.4*R1                                                     13900
      PHI=10.**(-2/XSAMP)                                               14000
      IF(BAUDS.GE.14.) PHI=1.                                           14100
      QA=0.                                                             14200
C                                                                       14300
300   RETURN                                                            14400
      END                                                               14500
C                                                                       14600
C                                                                       14700
C                                                                       14800
C                                                                       14900
C                                                                       15000
      SUBROUTINE PROBP(P,PIN,ISAVE,LKHD)                                15100
C                                                                       15200
C************************************************************           15300
C                                                                       15400
C    PROBP COMPUTES THE POSTERIOR PROBABILITY OF EACH                   15500
C    NEW PATH,                                                          15600
C                                                                       15700
C   VARIABLES:                                                          15800
C       P-     INPUT: SAVED PROBS OF PRIOR PATHS                        15900
C       OUTPUT:COMPUTED POSTERIOR PROBS OF NEW PATHS                    16000
C       PIN-   INPUT TRANSITION PROBABILITIES                           16100
C       LKHD-  INPUT LIKELIHOODS OF EACH TRANSITION                     16200
C       PSUM-  NORMALIZING CONSTANT (SUM OF P(J))                       16300
C                                                                       16400
C************************************************************           16500
C                                                                       16600
      REAL LKHD                                                         16700
      DIMENSION P( 750),PIN(25,30),LKHD( 750)                           16800
      DIMENSION PSAV( 750)                                              16900
C                                                                       17000
C                                                                       17100
      PMAX=0.                                                           17200
      PSUM=0.                                                           17300
C                                                                       17400
C   FOR EACH SAVED PATH, EACH TRANSITION:                               17500
C                                                                       17600
      DO 100 I=1,ISAVE                                                  17700
      DO 100 N=1,30                                                     17800
C                                                                       17900
C    COMPUTE IDENTITY OF NEW PATH:                                      18000
C                                                                       18100
      J=(I-1)*30+N                                                      18200
C                                                                       18300
C    PRODUCT OF PROBS, ADD TO PSUM                                      18400
C                                                                       18500
      PSAV(J)=P(I)*PIN(I,N)*LKHD(J)                                     18600
      PSUM=PSUM+PSAV(J)                                                 18700
C                                                                       18800
      IF(PSAV(J).LE.PMAX) GO TO 100                                     18900
      PMAX=PSAV(J)                                                      19000
C                                                                       19100
100   CONTINUE                                                          19200
C                                                                       19300
C                                                                       19400
C   NORMALIZE TO GET PROBABILITIES; SAVE:                               19500
C                                                                       19600
      NI=30*ISAVE                                                       19700
      DO 200 J=1,NI                                                     19800
      P(J)=PSAV(J)/PSUM                                                 19900
200   CONTINUE                                                          20000
C                                                                       20100
      RETURN                                                            20200
      END                                                               20300
C                                                                       20400
C                                                                       20500
C                                                                       20600
C                                                                       20700
C                                                                       20800
      SUBROUTINE SPROB(P,ISAVE,ILRSAV,PELM,KHAT,                        20900
     2  SPDHAT,PX)                                                      21000
C                                                                       21100
C*********************************************************              21200
C                                                                       21300
C   SPROB COMPUTES THE POSTERIOR PROBS OF THE ELEMENT                   21400
C   STATES,DATA RATE STATES,AND KEYSTATES BY SUMMING                    21500
C   OVER THE APPROPRIATE PATHS.                                         21600
C                                                                       21700
C   VARIABLE:                                                           21800
C       P-      INPUT PATH PROBABILITIES                                21900
C       ISAVE-  NUMBER OF PATHS SAVED                                   22000
C       PSELEM- OUTPUT ELEMENT PROB                                     22100
C       SPDHAT- OUTPUT SPEED ESTIMATE (DATA RATE WPM)                   22200
C       PX-     OUTPUT KEYSTATE PROBABILITY                             22300
C                                                                       22400
C*****************************************************                  22500
C                                                                       22600
      DIMENSION P(750),PSELEM(6),ILRSAV(750)                            22700
C                                                                       22800
C                                                                       22900
C                                                                       23000
C   INITIALIZE:                                                         23100
C                                                                       23200
      SPDHAT=0.                                                         23300
      PX=0.                                                             23400
C                                                                       23500
C   FOR EACH STATE EXTENSION OF PATH M:                                 23600
C   OBTAIN ELEMENT STATE PROBS,KEYSTATE PROBS,SPEED EST:                23700
C                                                                       23800
      DO 100 K=1,6                                                      23900
      PSELEM(K)=0.                                                      24000
      DO 100 I=1,5                                                      24100
C                                                                       24200
      N=(I-1)*6+K                                                       24300
C                                                                       24400
      DO 100 M=1,ISAVE                                                  24500
      J=(M-1)*30+N                                                      24600
      PSELEM(K)=PSELEM(K)+P(J)                                          24700
      SPDHAT=SPDHAT+ILRSAV(J)*P(J)                                      24800
      IF(K.GT.2) GO TO 100                                              24900
      PX=PX+P(J)                                                        25000
100   CONTINUE                                                          25100
C                                                                       25200
      PELM=0.                                                           25300
      DO 200 K=1,6                                                      25400
      IF(PSELEM(K).LT.PELM) GO TO 200                                   25500
      PELM=PSELEM(K)                                                    25600
      KHAT=K                                                            25700
200   CONTINUE                                                          25800
C                                                                       25900
      RETURN                                                            26000
      END                                                               26100
C                                                                       26200
C                                                                       26300
      SUBROUTINE SAVEP(P,PATHSV,ISAVE,IMAX,LAMSAV,                      26400
     2  DURSAV,ILRSAV,LAMBDA,DUR,ILRATE,SORT)                           26500
C                                                                       26600
C**************************************************************         26700
C                                                                       26800
C   THIS SUBROUTINE PERFORMS THE ALGORITHM TO SAVE                      26900
C   THE PATHS WITH HIGHEST POSTERIOR PROBABILITY.                       27000
C   IT WILL SAVE A MINIIMUM OF  7 PATHS (ONE FOR EACH                   27100
C   ELEMENT STATE AND ONE ADDITIONAL NODE), AND                         27200
C   A MAXIMUM OF 25 PATHS.  WITHIN THESE LIMITS, IT                     27300
C   SAVES ONLY ENOUGH TO MAKE THE TOTAL SAVED PROBABILITY               27400
C   EQUAL TO POPT.                                                      27500
C                                                                       27600
C   ADDITIONALLY, IT RESORTS THE LAMBDA,DUR,AND ILRATE                  27700
C   ARRAYS TO CORRESPOND TO THE SAVED NODES.                            27800
C                                                                       27900
C                                                                       28000
C   VARIABLES:                                                          28100
C       P-      INPUT PROBABILITY ARRAY OF NEW NODES                    28200
C       PATHSV- OUTPUT ARRAY OF THE PREVIOUS NODES TO                   28300
C               WHICH THE SAVED NODES ARE CONNECTED.                    28400
C       ISAVE-  INPUT: NO. OF PREVIOUS NODES SAVED                      28500
C               OUTPUT: NO. OF NODES SAVED AT CURRENT STAGE             28600
C       IMAX-   INDEX OF HIGHEST PROBABILITY NODE                       28700
C       LAMSAV- INPUT ARRAY OF LTR STATES AT EACH NEW NODE              28800
C       DURSAV- INPUT ARRAY OF SAVED DURATIONS                          28900
C       ILRSAV- INPUT ARRAY OF SAVED RATES                              29000
C       LAMBDA- OUTPUT ARRAY OF SAVED LTR STATES, SORTED                29100
C               ACCORDING TO PROBABILITY                                29200
C       DUR-    OUTPUT ARRAY OF SORTED DURATIONS                        29300
C       ILRATE- OUTPUT ARRAY OF SORTED RATES                            29400
C                                                                       29500
C**************************************************************         29600
C                                                                       29700
      INTEGER PATHSV,SORT                                               29800
      DIMENSION P( 750),PATHSV(25),PSAV(25),SORT(25)                    29900
      DIMENSION LAMSAV( 750),DURSAV( 750),ILRSAV( 750)                  30000
      DIMENSION LAMBDA(25),DUR(25),ILRATE(25)                           30100
      DIMENSION YKKIP(25),PKKIP(25)                                     30200
      DIMENSION YKKSV(750),PKKSV(750)                                   30300
      DIMENSION ICONV(25)                                               30400
C                                                                       30500
      COMMON/BLKSV1/YKKIP,PKKIP,YKKSV,PKKSV                             30600
C                                                                       30700
      DATA POPT/.90/                                                    30800
C                                                                       30900
      NSAV=0                                                            31000
      PSUM=0.                                                           31100
C                                                                       31200
C   SELECT SIX HIGHEST PROB ELEMENT STATE NODES:                        31300
C                                                                       31400
      DO 200 K=1,6                                                      31500
      PMAX=0.                                                           31600
      DO 100 IP=1,ISAVE                                                 31700
      DO 100 I=1,5                                                      31800
      J=(IP-1)*30+(I-1)*6+K                                             31900
      IF(P(J).LT.PMAX) GO TO 100                                        32000
      PMAX=P(J)                                                         32100
      JSAV=J                                                            32200
      IPSAV=IP                                                          32300
100   CONTINUE                                                          32400
      IF(PMAX.GE.0.000001) GO TO 150                                    32500
      GO TO 200                                                         32600
C                                                                       32700
C                                                                       32800
150   NSAV=NSAV+1                                                       32900
      PSUM=PSUM+PMAX                                                    33000
      PSAV(NSAV)=PMAX                                                   33100
      PATHSV(NSAV)=IPSAV                                                33200
      SORT(NSAV)=JSAV                                                   33300
200   CONTINUE                                                          33400
C                                                                       33500
C                                                                       33600
C                                                                       33700
C   SELECT ENOUGH ADDITIONAL NODES TO MAKE TOTAL                        33800
C   PROBABILITY SAVED EQUAL TO POPT,  OR A MAX                          33900
C   OF 25:                                                              34000
C                                                                       34100
520   PMAX=0.                                                           34200
      DO 500 IP=1,ISAVE                                                 34300
      DO 500 N=1,30                                                     34400
      J=(IP-1)*30+N                                                     34500
      DO 510 I=1,NSAV                                                   34600
      IF(J.EQ.SORT(I)) GO TO 500                                        34700
510   CONTINUE                                                          34800
C                                                                       34900
      IF(P(J).LE.PMAX) GO TO 500                                        35000
      PMAX=P(J)                                                         35100
      JSAV=J                                                            35200
      IPSAV=IP                                                          35300
500   CONTINUE                                                          35400
C                                                                       35500
      PSUM=PSUM+PMAX                                                    35600
      NSAV=NSAV+1                                                       35700
      PSAV(NSAV)=PMAX                                                   35800
      PATHSV(NSAV)=IPSAV                                                35900
      SORT(NSAV)=JSAV                                                   36000
      IF(PSUM.GE.POPT) GO TO 600                                        36100
      IF(NSAV.GE.25  ) GO TO 600                                        36200
      GO TO 520                                                         36300
C                                                                       36400
C                                                                       36500
C   NEW ISAVE EQUALS NO. OF NODES SAVED:                                36600
C                                                                       36700
600   ISAVE=NSAV                                                        36800
C                                                                       36900
C                                                                       37000
C   SORT THE SAVED ARRAYS TO OBTAIN THE ARRAYS                          37100
C   TO BE USED FOR THE NEXT ITERATION:                                  37200
C   ALSO OBTAIN HIGHEST PROBABILITY NODE:                               37300
C                                                                       37400
      DO 700 I=1,ISAVE                                                  37600
      P(I)=PSAV(I)/PSUM                                                 37700
      LAMBDA(I)=LAMSAV(SORT(I))                                         38100
      DUR(I)=DURSAV(SORT(I))                                            38200
      ILRATE(I)=ILRSAV(SORT(I))                                         38300
      YKKIP(I)=YKKSV(SORT(I))                                           38400
      PKKIP(I)=PKKSV(SORT(I))                                           38500
700   CONTINUE                                                          38600
C                                                                       38700
      DO 790 I=1,ISAVE                                                  38800
      ICONV(I)=1                                                        38900
790   CONTINUE                                                          39000
C                                                                       39100
      ISAVM1=ISAVE-1                                                    39200
      DO 800 N=1,ISAVM1                                                 39300
      IF(ICONV(N).EQ.0) GO TO 800                                       39400
C                                                                       39500
      NPLUS1=N+1                                                        39600
      DO 810 K=NPLUS1,ISAVE                                             39700
      IF(ICONV(K).EQ.0) GO TO 810                                       39800
C                                                                       39900
      IF(ILRATE(K).NE.ILRATE(N)) GO TO 810                              40000
      IF(DUR(K).NE.DUR(N)) GO TO 810                                    40100
      IF(LAMBDA(K).NE.LAMBDA(N)) GO TO 810                              40200
      ICONV(K)=0                                                        40300
C                                                                       40400
810   CONTINUE                                                          40500
800   CONTINUE                                                          40600
C                                                                       40700
      PSUM=0.                                                           40800
      N=1                                                               40900
      DO 900 I=2,ISAVE                                                  41000
      IF(ICONV(I).EQ.0) GO TO 900                                       41100
      N=N+1                                                             41200
      LAMBDA(N)=LAMBDA(I)                                               41300
      DUR(N)=DUR(I)                                                     41400
      ILRATE(N)=ILRATE(I)                                               41500
      YKKIP(N)=YKKIP(I)                                                 41600
      PKKIP(N)=PKKIP(I)                                                 41700
      PATHSV(N)=PATHSV(I)                                               41800
      SORT(N)=SORT(I)                                                   41900
      P(N)=P(I)                                                         42000
      PSUM=PSUM+P(N)                                                    42100
900   CONTINUE                                                          42200
C                                                                       42300
      ISAVE=N                                                           42400
C                                                                       42500
      PMAX=0.                                                           42550
      DO 950 I=1,ISAVE                                                  42600
      P(I)=P(I)/PSUM                                                    42700
      IF(P(I).LE.PMAX) GO TO 950                                        42710
      PMAX=P(I)                                                         42720
      IMAX=I                                                            42730
950   CONTINUE                                                          42800
C                                                                       42900
      RETURN                                                            43000
      END                                                               43100
C                                                                       43200
      SUBROUTINE TRELIS(ISAVE,PATHSV,LAMBDA,IMAX)                       00100
C                                                                       00200
C*****************************************************                  00300
C                                                                       00400
C   THIS SUBROUTINE STORES THE SAVED NODES AT EACH                      00500
C   STAGE AND FORMS THE TREE OF SAVED PATHS LINKING                     00600
C   THE NODES.  DECODING IS ACCOMPLISHED BY FINDING                     00700
C   THE CONVERGENT PATH IF IT OCCURS WITHIN A MAXIMUM                   00800
C   DELAY SET BY THE PARAMETER NDELAY.  IF CONVERGENCE                  00900
C   TO A SINGLE PATH DOES NOT OCCUR, THEN DECODING IS                   01000
C   DONE BY READING THE LETTER ON THE PATH WITH HIGHEST                 01100
C   PROBABILITY.                                                        01200
C                                                                       01300
C******************************************************                 01400
C                                                                       01500
      INTEGER PTHTRL,PATHSV                                             01600
      DIMENSION PATHSV(25),LAMBDA(25),PTHTRL(200,25)                    01700
      DIMENSION LMDSAV(200,25),IPNOD(25),LTRSV(200)                     01800
      COMMON/BLKEND/IEND                                                01900
C                                                                       02000
      DATA PTHTRL/5000*5/,LMDSAV/5000*5/                                02100
      DATA N/0/,NDELAY/200/                                             02200
      DATA IPNOD/25*1/,NCALL/0/,NMAX/0/,MMAX/0/                         02300
C                                                                       02400
C                                                                       02500
C   KEEP AVERAGE OF ISAVE,NDEL FOR DATA ANALYSIS:                       02600
C                                                                       02700
      NCALL=NCALL+1                                                     02800
      IF(IEND.NE.1) GO TO 10                                            02900
      ISAVG=XSAVG                                                       03000
      NDLAVG=XDLAVG                                                     03100
      IEND=0                                                            03200
C KZ4AP: change 1: TYPE 2000,...
      PRINT 2000,ISAVG,NDLAVG                                           03300
2000  FORMAT(1X,'AVG NO OF PATHS SAVED: ',I2,2X,                        03400
     2  'AVG DECODE DELAY: ',I3)                                        03500
C KZ4AP: change 1: TYPE 3000,...
      PRINT 3000,XMMAX,XNMAX                                            03600
3000  FORMAT(1X,'PERCENT OF TIME PATHS=25: ',F3.2,                      03700
     2  2X,'PERCENT OF TIME DELAY=200: ',F3.2)                          03800
C KZ4AP: change 1: the operator's wait ACCEPT 2000,WAIT removed.
C                                                                       03900
10    XSAVG=(XSAVG*(NCALL-1)+ISAVE)/NCALL                               04000
      XDLAVG=(XDLAVG*(NCALL-1)+NDEL)/NCALL                              04120
      IF(NDEL.NE.NDELAY) GO TO 20                                       04200
      NMAX=NMAX+1                                                       04300
      XNMAX=NMAX                                                        04400
      XNMAX=XNMAX/NCALL                                                 04500
20    IF(ISAVE.NE.25) GO TO 30                                          04600
      MMAX=MMAX+1                                                       04700
      XMMAX=MMAX                                                        04800
      XMMAX=XMMAX/NCALL                                                 04900
30    CONTINUE                                                          05000
C                                                                       05100
C                                                                       05200
C                                                                       05300
C   STORE PATHSV AND CORRESPONDING LAMBDA IN THE                        05400
C   TRELLIS USING A CIRCULAR BUFFER OF LENGTH NDELAY:                   05500
C                                                                       05600
      N=N+1                                                             05700
      IF(N.EQ.NDELAY+1) N=1                                             05800
      DO 100 I=1,ISAVE                                                  05900
      PTHTRL(N,I)=PATHSV(I)                                             06000
      LMDSAV(N,I)=LAMBDA(I)                                             06100
100   CONTINUE                                                          06200
C                                                                       06300
C                                                                       06400
C   PERFORM DYNAMIC PROGRAM ROUTINE TO FIND                             06500
C   CONVERGENT PATH:                                                    06600
C                                                                       06700
      K=0                                                               06800
      DO 180 I=1,ISAVE                                                  06900
      IPNOD(I)=I                                                        07000
180   CONTINUE                                                          07100
C                                                                       07200
190   K=K+1                                                             07300
      IF(K.EQ.NDELAY) GO TO 700                                         07400
      DO 200 IP=1,ISAVE                                                 07500
      I=N-K+1                                                           07600
      IF(I.LE.0) I=NDELAY+I                                             07700
      IPNOD(IP)=PTHTRL(I,IPNOD(IP))                                     07800
      IF(IP.EQ.IMAX) IPMAX=IPNOD(IP)                                    07900
200   CONTINUE                                                          08000
C                                                                       08100
C   IF ALL NODES ARE EQUAL, THEN PATHS CONVERGE:                        08200
C                                                                       08300
      DO 300 IEQ=2,ISAVE                                                08400
      IF(IPNOD(1).NE.IPNOD(IEQ)) GO TO 190                              08500
300   CONTINUE                                                          08600
C                                                                       08700
C                                                                       08800
C   PATHS CONVERGE; SET NDEL:                                           08900
C                                                                       09000
      NDEL=K+1                                                          09100
C                                                                       09200
C                                                                       09300
C   IF POINT OF CONVERGENCE IS SAME AS IT WAS ON                        09400
C   LAST CALL, THEN NO NEED TO RE-DECODE SAME NODE:                     09500
C                                                                       09600
      IF(NDEL.EQ.NDELST+1) GO TO 800                                    09700
C                                                                       09800
C                                                                       09900
C   IF POINT OF CONVERGENCE OCCURS AT SAME DELAY AS                     10000
C   LAST CALL, THEN TRANSLATE:                                          10100
C                                                                       10200
      IF(NDEL.NE.NDELST) GO TO 350                                      10300
      I=N-NDEL+1                                                        10400
      IF(I.LE.0) I=NDELAY+I                                             10500
      LTR=LMDSAV(I,IPNOD(1))                                            10600
      CALL TRANSL(LTR)                                                  10700
      GO TO 800                                                         10800
C                                                                       10900
C   OTHERWISE, POINT OF CONVERGENCE HAS OCCURED                         11000
C   EARLIER ON THIS CALL, SO NEED TO TRANSLATE                          11100
C   EVERYTHING ON THE CONVERGENT PATH FROM                              11200
C   PREVIOUS POINT OF CONVERGENCE TO THIS POINT:                        11300
C                                                                       11400
C                                                                       11500
350   KD=0                                                              11600
      IP=IPNOD(1)                                                       11700
      DO 400 K=NDEL,NDELST                                              11800
      KD=KD+1                                                           11900
      I=N-K+1                                                           12000
      IF(I.LE.0) I=NDELAY+I                                             12100
      LTRSV(KD)=LMDSAV(I,IP)                                            12200
      IP=PTHTRL(I,IP)                                                   12300
400   CONTINUE                                                          12400
C                                                                       12500
C                                                                       12600
C   REVERSE ORDER OF DECODED LETTERS, SINCE THEY                        12700
C   WERE OBTAINED FROM THE TRELLIS IN REVERSE;                          12800
C   TRANSLATE EACH:                                                     12900
C                                                                       13000
      DO 500 I=1,KD                                                     13100
      LTR=LTRSV(KD-I+1)                                                 13200
      CALL TRANSL(LTR)                                                  13300
500   CONTINUE                                                          13400
      GO TO 800                                                         13500
C                                                                       13600
700   CONTINUE                                                          13700
C                                                                       13800
C                                                                       13900
C   PATHS HAVE NOT CONVERGED AT MAXIMUM ALLOWABLE                       14000
C   DELAY, SO TRANSLATE WHAT IS ON HIGHEST                              14100
C   PROBABILITY PATH:                                                   14200
C                                                                       14300
      NDEL=NDELAY                                                       14400
      I=N-NDELAY+1                                                      14500
      IF(I.LE.0) I=NDELAY+I                                             14600
      LTR=LMDSAV(I,IPMAX)                                               14700
      CALL TRANSL(LTR)                                                  14800
C                                                                       14900
C                                                                       15000
C   PRUNE AWAY NODES WHICH ARE NOT ON                                   15100
C   THIS PATH:                                                          15200
C                                                                       15300
      DO 750 K=1,ISAVE                                                  15400
      IF(IPNOD(K).EQ.IPMAX) GO TO 750                                   15500
      LAMBDA(K)=0                                                       15600
750   CONTINUE                                                          15700
C                                                                       15800
C                                                                       15900
800   NDELST=NDEL                                                       16000
      RETURN                                                            16100
      END                                                               16200
C                                                                       16300
C                                                                       16400
C                                                                       16500
C                                                                       16600
C                                                                       16700
      SUBROUTINE TRANSL(LTR)                                            16800
C                                                                       16900
C**********************************************************             17000
C                                                                       17100
C   THIS SUBROUTINE PRODUCES THE OUTPUT TEXT ON A CRT.                  17200
C   IT USES THE SIMPLE FORMATTING RULES DESCRIBED IN THE                17300
C   TEXT.                                                               17400
C                                                                       17500
C**********************************************************             17600
C                                                                       17700
      INTEGER SPFLAG,ELMHAT,ELMOUT                                      17800
      DIMENSION LTRMAP(400),IALPH(70),IBLANK(400)                       17900
      DIMENSION IELMST(400),ILAM1(16),ILAMX(6)                          18000
C                                                                       18100
      COMMON/BLKTRN/LTRMAP,IALPH,IBLANK                                 18200
      COMMON/BLKLAM/IELMST,ILAM1,ILAMX                                  18300
C KZ4AP: change 4: the trace level and the decoder step.
      COMMON/KZ4AP/RATE2,KZTRC,KZLAW,NSTOP,NSW,KZSTP
C                                                                       18400
      DATA ISPACE/' '/,SPFLAG/0/,NCHAR/0/                               18500
C                                                                       18600
C                                                                       18700
C   DETERMINE IF A CSP,WSP,OR PAUSE TO MARK TRANSITION                  18800
C   HAS OCCURED; IF SO LTR IS READY FOR OUTPUT:                         18900
C                                                                       19000
C KZ4AP: change 4: KZC counts the calls, one per decided trellis
C KZ4AP: stage in time order; record D: each decided state (trace
C KZ4AP: level 2), with the current decoder step KZSTP.
      KZC=KZC+1
      IF(KZTRC.GE.2) WRITE(22,9191) KZC,KZSTP,LTR
9191  FORMAT('D ',I8,1X,I8,1X,I3)
      ELMHAT=ILAM1(IELMST(LTR))                                         19100
      IXL=ILAMX(ELMHAT)                                                 19200
      IF(IXL.EQ.IXLAST) GO TO 700                                       19300
      IF((IXL.EQ.1).AND.(LSTELM.GE.4)) GO TO 10                         19400
      IF((IXL.EQ.0).AND.(LSTELM.LE.2)) GO TO 700                        19500
      GO TO 700                                                         19600
C                                                                       19700
10    LTRHAT=LSTLTR                                                     19800
      LTROUT=IALPH(LTRMAP(LTRHAT))                                      19900
      NBLANK=IBLANK(LTRHAT)                                             20000
      ELMOUT=ILAM1(IELMST(LTRHAT))                                      20100
      GO TO 40                                                          20200
C KZ4AP: change 3: TYPE 5000,ELMOUT
      WRITE(21,5000) ELMOUT                                             20300
5000  FORMAT(1X,I1,$)                                                   20400
      NCHAR=NCHAR+1                                                     20500
C                                                                       20600
C                                                                       20700
40    CONTINUE                                                          20800
C KZ4AP: change 4: record L: each letter printed, with its code and
C KZ4AP: its NBLANK (trace level 1).
      IF(KZTRC.GE.1) WRITE(22,9208) KZC,KZSTP,LTRMAP(LTRHAT),NBLANK
9208  FORMAT('L ',I8,1X,I8,1X,I2,1X,I1)
      IF(LTRMAP(LTRHAT).EQ.43) GO TO 50                                 20900
      IF(LTRMAP(LTRHAT).LE.44) GO TO 100                                21000
      IF(LTRMAP(LTRHAT).LE.46) GO TO 200                                21100
      IF(LTRMAP(LTRHAT).LE.60) GO TO 300                                21200
      IF(LTRMAP(LTRHAT).EQ.61) GO TO 400                                21300
      IF(LTRMAP(LTRHAT).EQ.66) GO TO 500                                21400
      GO TO 550                                                         21500
C                                                                       21600
50    IF(SPFLAG.EQ.0) GO TO 100                                         21700
C                                                                       21800
      NCHAR=0                                                           21900
C KZ4AP: change 3: TYPE 1500,LTROUT
      WRITE(21,1500) LTROUT                                             22000
1500  FORMAT(2X,A1,/)                                                   22100
      SPFLAG=1                                                          22200
      GO TO 600                                                         22300
C                                                                       22400
100   NCHAR=NCHAR+1                                                     22500
C KZ4AP: change 3: TYPE 1000,LTROUT
      WRITE(21,1000) LTROUT                                             22600
1000  FORMAT(1X,A1,$)                                                   22700
      SPFLAG=0                                                          22800
C KZ4AP: change 7: GO TO 110 jumped to the terminal statement of
C KZ4AP: DO 110 from outside the loop; redirected to 9110 after it.
      IF(NBLANK.EQ.0) GO TO 9110                                        22900
      SPFLAG=1                                                          23000
      DO 110 I=1,NBLANK                                                 23100
      NCHAR=NCHAR+1                                                     23200
C KZ4AP: change 3: TYPE 1000,ISPACE
      WRITE(21,1000) ISPACE                                             23300
110   CONTINUE                                                          23400
9110  CONTINUE
      GO TO 600                                                         23500
C                                                                       23600
200   NCHAR=NCHAR+2                                                     23700
C KZ4AP: change 3: TYPE 1100,LTROUT
      WRITE(21,1100) LTROUT                                             23800
1100  FORMAT(1X,A2,$)                                                   23900
      SPFLAG=0                                                          24000
C KZ4AP: change 7: GO TO 210 jumped to the terminal statement of
C KZ4AP: DO 210 from outside the loop; redirected to 9210 after it.
      IF(NBLANK.EQ.0) GO TO 9210                                        24100
      SPFLAG=1                                                          24200
      DO 210 I=1,NBLANK                                                 24300
      NCHAR=NCHAR+1                                                     24400
C KZ4AP: change 3: TYPE 1000,ISPACE
      WRITE(21,1000) ISPACE                                             24500
210   CONTINUE                                                          24600
9210  CONTINUE
      GO TO 600                                                         24700
C                                                                       24800
300   NCHAR=NCHAR+4                                                     24900
C KZ4AP: change 3: TYPE 1200,LTROUT
      WRITE(21,1200) LTROUT                                             25000
1200  FORMAT(2X,A2,2X,$)                                                25100
      SPFLAG=1                                                          25200
C KZ4AP: change 7: GO TO 310 jumped to the terminal statement of
C KZ4AP: DO 310 from outside the loop; redirected to 9310 after it.
      IF(NBLANK.EQ.0) GO TO 9310                                        25300
      DO 310 I=1,NBLANK                                                 25400
      NCHAR=NCHAR+1                                                     25500
C KZ4AP: change 3: TYPE 1000,ISPACE
      WRITE(21,1000) ISPACE                                             25600
310   CONTINUE                                                          25700
9310  CONTINUE
      GO TO 600                                                         25800
C                                                                       25900
400   NCHAR=NCHAR+5                                                     26000
C KZ4AP: change 3: TYPE 1300,LTROUT
      WRITE(21,1300) LTROUT                                             26100
1300  FORMAT(2X,A3,2X,$)                                                26200
      SPFLAG=1                                                          26300
      GO TO 600                                                         26400
C                                                                       26500
500   NCHAR=0                                                           26600
C KZ4AP: change 3: TYPE 1400,LTROUT
      WRITE(21,1400) LTROUT                                             26700
1400  FORMAT(/,10X,A2,/,10X)                                            26800
      SPFLAG=1                                                          26900
      GO TO 600                                                         27000
C                                                                       27100
550   NCHAR=NCHAR+5                                                     27200
C KZ4AP: change 3: TYPE 1700,LTROUT
      WRITE(21,1700) LTROUT                                             27300
1700  FORMAT(2X,A3,2X,$)                                                27400
      SPFLAG=0                                                          27500
C KZ4AP: change 7: GO TO 560 jumped to the terminal statement of
C KZ4AP: DO 560 from outside the loop; redirected to 9560 after it.
      IF(NBLANK.EQ.0) GO TO 9560                                        27600
      SPFLAG=1                                                          27700
      DO 560 I=1,NBLANK                                                 27800
      NCHAR=NCHAR+1                                                     27900
C KZ4AP: change 3: TYPE 1000,ISPACE
      WRITE(21,1000) ISPACE                                             28000
560   CONTINUE                                                          28100
9560  CONTINUE
C                                                                       28200
600   IF(NCHAR.LT.52) GO TO 700                                         28300
C                                                                       28400
C KZ4AP: change 3: TYPE 1600
      WRITE(21,1600)                                                    28500
1600  FORMAT(/,10X)                                                     28600
      NCHAR=0                                                           28700
C                                                                       28800
700   IXLAST=IXL                                                        28900
      LSTELM=ELMHAT                                                     29000
      LSTLTR=LTR                                                        29100
C                                                                       29200
      RETURN                                                            29300
      END                                                               29400
C                                                                       29500
      SUBROUTINE RCVR(ZIN,ZOUT)                                         00100
C                                                                       00200
C************************************************************           00300
C                                                                       00400
C   THIS SUBROUTINE CONVERTS THE INPUT SIGNAL AT                        00500
C   RADIAN FREQ WC TO 1000 HZ.                                          00600
C                                                                       00700
C************************************************************           00800
C                                                                       00900
      COMMON/BLK1/TAU/BLK2/WC                                           01000
C                                                                       01100
      DATA THETA/0./,THETLO/0./                                         01200
C                                                                       01300
      THETA=THETA+WC*TAU                                                01400
      THETA=AMOD(THETA,6.28319)                                         01500
C                                                                       01600
      ZI=ZIN*COS(THETA)                                                 01700
      ZQ=ZIN*SIN(THETA)                                                 01800
      ZILP=ZILP+.070*(ZI-ZILP)                                          01900
      ZQLP=ZQLP+.070*(ZQ-ZQLP)                                          02000
C                                                                       02100
      THETLO=THETLO+6283.2*TAU                                          02200
      THETLO=AMOD(THETLO,6.28319)                                       02300
C                                                                       02400
      ZOUT= ZILP*COS(THETLO)+ZQLP*SIN(THETLO)                           02500
C                                                                       02600
      RETURN                                                            02700
      END                                                               02800
C                                                                       02900
C                                                                       03000
C                                                                       03100
C                                                                       03200
C                                                                       03300
C                                                                       03400
C                                                                       03500
C                                                                       03600
      SUBROUTINE BPFDET(ZIN,Z)                                          03700
C                                                                       03800
C************************************************************           03900
C                                                                       04000
C   THIS SUBROUTINE IMPLEMENTS THE BANDPASS FILTER AND                  04100
C   ENVELOPE DETECTOR FUNCTIONS.  THE BPF IS A SIMPLE CASCADE           04200
C   OF TWO 2-POLE DIGITAL RESONATORS AT A CENTER FREQ OF                04300
C   1000 HZ. THE LPF OF THE ENVELOPE DETECTOR IS A                      04400
C   THREE-POLE CHEBYSCHEV 100 HZ LPF.                                   04500
C                                                                       04600
C************************************************************           04700
C                                                                       04800
C                                                                       04900
      DIMENSION A(4)                                                    05000
C                                                                       05100
      DATA A/.000030051,2.9507982,2.90396345,-.953135172/               05200
      DATA CK1/1.37158/,CK2/.9409/,CG/.0150/                            05300
      DATA C1/1.2726/,C2/.8100/,C/.1900/                                05400
C                                                                       05500
C                                                                       05600
C   BPF IS TWO 2-POLE RESONATORS:                                       05700
C                                                                       05800
      W3=W2                                                             05900
      W2=W1                                                             06000
      W1=C1*W2-C2*W3+C*ZIN                                              06100
C                                                                       06200
      X3=X2                                                             06300
      X2=X1                                                             06400
      X1=CK1*X2-CK2*X3+CG*W1                                            06500
      ZBPF=X1                                                           06600
C                                                                       06700
C                                                                       06800
C   ENVELOPE DETECTOR (SQUARE-LAW):                                     06900
C    SQUARE-                                                            07000
C                                                                       07100
      XDET=SQRT(ZBPF**2)                                                07200
C                                                                       07300
C                                                                       07400
C    LOW-PASS FILTER-                                                   07500
C                                                                       07600
C                                                                       07700
      Y3=Y2                                                             07800
      Y2=Y1                                                             07900
      Y1=Y0                                                             08000
      Y0=XDET*A(1)                                                      08100
C                                                                       08200
      Z3=Z2                                                             08300
      Z2=Z1                                                             08400
      Z1=Z                                                              08500
      Z=Y0+3.*(Y1+Y2)+Y3                                                08600
      Z=Z+A(2)*Z1-A(3)*Z2-A(4)*Z3                                       08700
C                                                                       08800
C                                                                       08900
      RETURN                                                            09000
      END                                                               09100
C                                                                       09200
C                                                                       09300
C                                                                       09400
C                                                                       09500
      SUBROUTINE NOISE(ZIN,RN,Z)                                        09600
C                                                                       09700
C************************************************************           09800
C                                                                       09900
C   THIS SUBROUTINE ESTIMATES THE NOISE POWER IN THE                    10000
C   ENVELOPE DETECTED OUTPUT FOR USE BY THE KALMAN                      10100
C   FILTERS.  AN ESTIMATE OF THE NOISE POWER IS                         10200
C   ALSO SUBTRACTED FROM THE ENVELOPE DETECTED SIGNAL                   10300
C   IN ORDER TO PRODUCE A ZERO-MEAN NOISE PROCESS                       10400
C   AT THE INPUT TO THE MORSE PROCESSOR.                                10500
C                                                                       10600
C************************************************************           10700
C                                                                       10800
      DIMENSION YLONG(200),YSHORT(50)                                   10900
C                                                                       11000
      DATA YLONG/200*1./,YSHORT/50*1./                                  11100
      DATA KL/1/,KKL/1/,KS/1/,KKS/1/                                    11200
      DATA YMIN1/1./,YMIN2/1./,YMAVG/.05/                               11300
C                                                                       11400
      KL=KL+1                                                           11500
      IF(KL.EQ.201) KL=1                                                11600
      KS=KS+1                                                           11700
      IF(KS.EQ.51)KS=1                                                  11800
      KKL=KKL+1                                                         11900
      IF(KKL.GE.200) KKL=200                                            12000
      KKS=KKS+1                                                         12100
      IF(KKS.GE.50) KKS=50                                              12200
C                                                                       12300
      IF(KKS.LE.2) GO TO 10                                             12400
      YLONG(KL)=ZIN                                                     12500
      YSHORT(KS)=ZIN                                                    12600
      YMIN1=ZIN                                                         12700
      YMIN2=ZIN                                                         12800
C                                                                       12900
10    DO 100 I=1,KKL                                                    13000
      IF(YLONG(I).GT.YMIN1) GO TO 100                                   13100
      YMIN1=YLONG(I)                                                    13200
100   CONTINUE                                                          13300
C                                                                       13400
      DO 200 I=1,KKS                                                    13500
      IF(YSHORT(I).GT.YMIN2) GO TO 200                                  13600
      YMIN2=YSHORT(I)                                                   13700
200   CONTINUE                                                          13800
C                                                                       13900
      YMIN=YMIN1                                                        14000
      IF(YMIN2.LT.YMIN1) YMIN=YMIN2                                     14100
C                                                                       14200
      YMAVG=YMAVG+.004*(YMIN-YMAVG)                                     14300
C                                                                       14400
      RN=0.30*YMAVG                                                     14500
      IF(RN.LT.0.005) RN=0.005                                          14600
      Z=1.1*(ZIN-2.4*YMAVG-.05)                                         14700
C                                                                       14800
      RETURN                                                            14900
      END                                                               15000
C KZ4AP: change 2: RANDN(W,N,XMEAN,SIGMA), a library routine missing
C KZ4AP: from the listing: one Gaussian number with mean XMEAN and
C KZ4AP: standard deviation SIGMA (SIMSG1 passes the square roots of
C KZ4AP: the variances), by Box-Muller from two uniform numbers of the
C KZ4AP: one random stream; called also when SIGMA = 0. N (always 1)
C KZ4AP: is not used.
      SUBROUTINE RANDN(W,N,XMEAN,SIGMA)
      CALL RANDOM_NUMBER(U1)
      CALL RANDOM_NUMBER(U2)
      W=XMEAN+SIGMA*SQRT(-2.*LOG(1.-U1))*COS(6.283185307179586*U2)
      RETURN
      END
C KZ4AP: change 8: the display library routines ERASE, PLOTR and VIEW
C KZ4AP: (DISPLA, p. 166) are missing; stubs that do nothing.
      SUBROUTINE ERASE
      RETURN
      END
      SUBROUTINE PLOTR(S,N,IFLAG,SCALE,IY)
      DIMENSION S(*)
      RETURN
      END
      SUBROUTINE VIEW(C)
      CHARACTER*(*) C
      RETURN
      END
C KZ4AP: change 4: the trace of one decoder step (trace level 2), in
C KZ4AP: STEPS.BIN, float64: the step, z (AU), R (AU2), ISAVE, IMAX,
C KZ4AP: PX, XHAT, SPDHAT (WPM); then, for each saved path I = 1 ..
C KZ4AP: ISAVE, its state LAMBDA, DUR (ms), ILRATE (WPM), PATHSV, P,
C KZ4AP: YKKIP (AU) and PKKIP (AU2), as PROCES holds them after TRELIS.
      SUBROUTINE KZSTEP(Z,RN,ISAVE,IMAX,PX,XHAT,SPDHAT,LAMBDA,DUR,
     2  ILRATE,PATHSV,P)
      INTEGER XHAT,PATHSV
      DIMENSION LAMBDA(25),DUR(25),ILRATE(25),PATHSV(25),P(750)
      DIMENSION YKKIP(25),PKKIP(25),YKKSV(750),PKKSV(750)
      COMMON/BLKSV1/YKKIP,PKKIP,YKKSV,PKKSV
      COMMON/KZ4AP/RATE2,KZTRC,KZLAW,NSTOP,NSW,KZSTP
      WRITE(23) DBLE(KZSTP),Z,RN,DBLE(ISAVE),DBLE(IMAX),PX,
     2  DBLE(XHAT),SPDHAT
      DO 10 I=1,ISAVE
      WRITE(23) DBLE(LAMBDA(I)),DUR(I),DBLE(ILRATE(I)),
     2  DBLE(PATHSV(I)),P(I),YKKIP(I),PKKIP(I)
10    CONTINUE
      RETURN
      END
