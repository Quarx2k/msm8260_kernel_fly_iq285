
/**********************************************************
  DEVICE   : mxT224  0.2
  CUSTOMER : KT Tech
  PROJECT  : O3
  X SIZE   : X19
  Y SIZE   : Y11
  THRESHOLD : 0x20
***********************************************************/

#define __MXT224_CONFIG__

/* Calibration Configuration Information */
#define T9_TCHTHR_CAL		      30

/* TA mode Configurations */
#define T9_TCHTHR_TA              75
#define T9_TCHDI_TA               3
#define T9_MOVHYSTI_TA            10
#define T9_MOVFILTER_TA           14
#define T22_NOISETHR_TA           60


/* SPT_USERDATA_T38 INSTANCE 0 */
#define T38_USERDATA0             0
#define T38_USERDATA1             5	     /* CAL_THR */
#define T38_USERDATA2             3     /* num_of_antitouch */
#define T38_USERDATA3             20 	 /* touch skip size in calibration  */
#define T38_USERDATA4             0	     /* MXT_ADR_T8_ATCHFRCCALRATIO for normal */
#define T38_USERDATA5             0		
#define T38_USERDATA6             0
#define T38_USERDATA7			  4		/* max touch for check_auto_cal */

/* GEN_POWERCONFIG_T7 */

#define T7_IDLEACQINT             32
#define T7_ACTVACQINT             20
#define T7_ACTV2IDLETO            50

/* _GEN_ACQUISITIONCONFIG_T8 INSTANCE 0 */
#define T8_CHRGTIME				  10      /* 6 - 60  * 83 ns */
#define T8_RESERVED	              0
#define T8_TCHDRIFT               5
#define T8_DRIFTST                5 
#define T8_TCHAUTOCAL             0
#define T8_SYNC                   0
#define T8_ATCHCALST			  9
#define T8_ATCHCALSTHR			  5
#define T8_ATCHFRCCALTHR		  127		/* V2.0 added */
#define T8_ATCHFRCCALRATIO		  127		/* V2.0 added */

/* TOUCH_MULTITOUCHSCREEN_T9 INSTANCE 0 */
#define T9_CTRL                   0x8B
#define T9_XORIGIN                0
#define T9_YORIGIN                0
#define T9_XSIZE                  19
#define T9_YSIZE                  11
#define T9_AKSCFG                 0
#define T9_BLEN                   32
#define T9_TCHTHR		  		  55
#define T9_TCHDI                  2
#define T9_ORIENT                 1
#define T9_MRGTIMEOUT             0
#define T9_MOVHYSTI               3
#define T9_MOVHYSTN               1
#define T9_MOVFILTER              13
#define T9_NUMTOUCH               10    
#define T9_MRGHYST                3	    
#define T9_MRGTHR                 50	
#define T9_AMPHYST                6
#define T9_XRANGE                 (1024-1)
#define T9_YRANGE                 (540-1)
#define T9_XLOCLIP                5
#define T9_XHICLIP                5
#define T9_YLOCLIP                19
#define T9_YHICLIP                25
#define T9_XEDGECTRL              136 
#define T9_XEDGEDIST              70	
#define T9_YEDGECTRL              202   
#define T9_YEDGEDIST              70	
#define T9_JUMPLIMIT              15
#define T9_TCHHYST                0	 /* V2.0 or MXT224E added */

/* TOUCH_KEYARRAY_T15 */
#define T15_CTRL                  0 /* single key configuration*/  /* 0x03 = multi-key */
#define T15_XORIGIN               0
#define T15_YORIGIN			      0
#define T15_XSIZE				  0 
#define T15_YSIZE                 0
#define T15_AKSCFG                0
#define T15_BLEN                  0
#define T15_TCHTHR                0
#define T15_TCHDI                 0
#define T15_RESERVED_0            0
#define T15_RESERVED_1            0

/* SPT_COMMSCONFIG_T18 */
#define T18_CTRL                  0
#define T18_COMMAND               0

/* SPT_GPIOPWM_T19 INSTANCE 0 */
#define T19_CTRL                  0
#define T19_REPORTMASK            0
#define T19_DIR                   0
#define T19_INTPULLUP             0
#define T19_OUT                   0
#define T19_WAKE                  0
#define T19_PWM                   0
#define T19_PERIOD                0
#define T19_DUTY_0                0
#define T19_DUTY_1                0
#define T19_DUTY_2                0
#define T19_DUTY_3                0
#define T19_TRIGGER_0             0
#define T19_TRIGGER_1             0
#define T19_TRIGGER_2             0
#define T19_TRIGGER_3             0


/* GRIPSUPPRESSION_T20 */
#define T20_CTRL                  7
#define T20_XLOGRIP               0
#define T20_XHIGRIP               0
#define T20_YLOGRIP               0
#define T20_YHIGRIP               0
#define T20_MAXTCHS               0
#define T20_RESERVED              0
#define T20_SZTHR1                30
#define T20_SZTHR2                20
#define T20_SHPTHR1               4
#define T20_SHPTHR2               15
#define T20_SUPEXTTO              0

/* NOISESUPPRESSION_T22 */
#define T22_CTRL                  133 
#define T22_RESERVED              0
#define T22_RESERVED1             0 
#define T22_GCAFUL1               0
#define T22_GCAFUL2               0
#define T22_GCAFLL1               0
#define T22_GCAFLL2               0
#define T22_ACTVGCAFVALID         3
#define T22_NOISETHR              58
#define T22_RESERVED2             0
#define T22_FREQHOPSCALE          0
#define T22_FREQ0                 9
#define T22_FREQ1                 15
#define T22_FREQ2                 24
#define T22_FREQ3                 34
#define T22_FREQ4                 255
#define T22_IDLECAFVALID          3

/* TOUCH_PROXIMITY_T23 */
#define T23_CTRL                  0
#define T23_XORIGIN               0
#define T23_YORIGIN               0
#define T23_XSIZE                 0
#define T23_YSIZE                 0
#define T23_RESERVED              0
#define T23_BLEN                  0
#define T23_FXDDTHR               0
#define T23_FXDDI                 0
#define T23_AVERAGE               0
#define T23_MVNULLRATE            0
#define T23_MVDTHR                0

/* T24_[PROCI_ONETOUCHGESTUREPROCESSOR_T24 INSTANCE 0] */
#define T24_CTRL                  0
#define T24_NUMGEST               0
#define T24_GESTEN                0
#define T24_PROCESS               0
#define T24_TAPTO                 0
#define T24_FLICKTO               0
#define T24_DRAGTO                0
#define T24_SPRESSTO              0
#define T24_LPRESSTO              0
#define T24_REPPRESSTO            0
#define T24_FLICKTHR              0
#define T24_DRAGTHR               0
#define T24_TAPTHR                0
#define T24_THROWTHR              0


/* [SPT_SELFTEST_T25 INSTANCE 0] */
#define T25_CTRL                  0
#define T25_CMD                   0
#define T25_SIGLIM_0_UPSIGLIM     13500
#define T25_SIGLIM_0_LOSIGLIM     5500
#define T25_SIGLIM_1_UPSIGLIM     13500
#define T25_SIGLIM_1_LOSIGLIM     5500
#define T25_SIGLIM_2_UPSIGLIM     0
#define T25_SIGLIM_2_LOSIGLIM     0

/* [MXT_SPT_CTECONFIG_T28] */
#define T28_CTRL                  1
#define T28_CMD					  0
#define T28_MODE                  3
#define T28_IDLEGCAFDEPTH		  32
#define T28_ACTVGCAFDEPTH         63
#define T28_VOLTAGE				  0

/********************* END  *********************/

