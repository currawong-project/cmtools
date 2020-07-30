#include "cmPrefix.h"
#include "cmGlobal.h"
#include "cmRpt.h"
#include "cmErr.h"
#include "cmCtx.h"
#include "cmMem.h"
#include "cmMallocDebug.h"
#include "cmLinkedHeap.h"
#include "cmFileSys.h"
#include "cmText.h"

#include "cmPgmOpts.h"

#include "cmTime.h"
#include "cmAudioPort.h"
#include "cmApBuf.h"  // only needed for cmApBufTest().
#include "cmAudioPortFile.h"
#include "cmAudioAggDev.h"
#include "cmAudioNrtDev.h"

#include "cmFloatTypes.h"
#include "cmAudioFile.h"
#include "cmFile.h"

enum
{
 kOkAdRC = cmOkRC,
 kAudioPortFailAdRC,
 kAudioPortFileFailAdRC,
 kAudioPortNrtFailAdRC,
 kAudioBufFailAdRC
};

const cmChar_t* poBegHelpStr =
  "audiodev Test the real-time audio ports"
  "\n"
  "audiodev -i<in_dev_index> -o <out_dev_index -s <sample_rate>\n"
  "\n"
  "All arguments are optional. The default input and output device index is 0.\n"
  "\n";
  
const cmChar_t* poEndHelpStr = "";
  
/// [cmAudioPortExample]

// See cmApPortTest() below for the main point of entry.

// Data structure used to hold the parameters for cpApPortTest()
// and the user defined data record passed to the host from the
// audio port callback functions.
typedef struct
{
  unsigned      bufCnt;         // 2=double buffering 3=triple buffering
  unsigned      chIdx;          // first test channel
  unsigned      chCnt;          // count of channels to test
  unsigned      framesPerCycle; // DSP frames per cycle
  unsigned      bufFrmCnt;      // count of DSP frames used by the audio buffer  (bufCnt * framesPerCycle)
  unsigned      bufSmpCnt;      // count of samples used by the audio buffer     (chCnt  * bufFrmCnt)
  unsigned      inDevIdx;       // input device index
  unsigned      outDevIdx;      // output device index
  double        srate;          // audio sample rate
  unsigned      meterMs;        // audio meter buffer length

  // param's and state for cmApSynthSine()
  unsigned      phase;          // sine synth phase
  double        frqHz;          // sine synth frequency in Hz

  // buffer and state for cmApCopyIn/Out()
  cmApSample_t* buf;            // buf[bufSmpCnt] - circular interleaved audio buffer
  unsigned      bufInIdx;       // next input buffer index
  unsigned      bufOutIdx;      // next output buffer index
  unsigned      bufFullCnt;     // count of full samples

  // debugging log data arrays 
  unsigned      logCnt;        // count of elements in log[] and ilong[]
  char*         log;           // log[logCnt]
  unsigned*     ilog;          // ilog[logCnt]
  unsigned      logIdx;        // current log index

  unsigned      cbCnt;         // count the callback
} cmApPortTestRecd;

unsigned _cmGlobalInDevIdx  = 0;
unsigned _cmGlobalOutDevIdx = 0;
unsigned _cmGlobalCbCnt     = 0;

#define aSrate  48000
#define aFrmN  aSrate*10
#define aChN   2
#define abufN  aFrmN*aChN

cmApSample_t abuf[ abufN ];
unsigned     abufi  = 0;

void _abuf_copy_in( cmApAudioPacket_t* pktArray, unsigned pktN )
{
  unsigned i,j,k;
  for(i=0; i<pktN; ++i)
  {
    cmApSample_t* sp   = (cmApSample_t*)(pktArray[i].audioBytesPtr);
    unsigned      frmN = pktArray[i].audioFramesCnt;
    unsigned      chN  = cmMin(pktArray[i].chCnt,aChN);
    
    for(j=0; abufi<aFrmN && j<frmN; ++j, ++abufi)
      for(k=0; k<chN; ++k)
        abuf[ (k*aFrmN) + abufi ] = *sp++;

  }

  
}
  
void _abuf_write_audio_file(cmCtx_t* ctx )
{
  cmApSample_t* sigVV[ ] = { abuf, abuf + aFrmN };
  cmAudioFileWriteFileFloat(   "/home/kevin/temp/temp.wav", aSrate, 16, aFrmN, 2, sigVV, &ctx->rpt );
}

void _abuf_write_csv_file(cmCtx_t* ctx )
{
  cmFileH_t fH = cmFileNullHandle;
  unsigned i = 0,j;
  cmFileOpen( &fH, "/home/kevin/temp/temp.csv", kWriteFileFl, &ctx->rpt );

  for(i=0; i<aFrmN; ++i)
  {
    for(j=0; j<aChN; ++j)
    {
      char comma = j==aChN-1 ? ' ':',';
      
      cmFilePrintf(fH, "%f%c",abuf[ aFrmN*j + i ], comma );
    }
       
    cmFilePrintf(  fH, "\n");
  }
  
  cmFileClose(&fH);
}

void _cmApPortCb2( cmApAudioPacket_t* inPktArray, unsigned inPktCnt, cmApAudioPacket_t* outPktArray, unsigned outPktCnt )
{
  cmApBufInputToOutput( _cmGlobalInDevIdx, _cmGlobalOutDevIdx );

  cmApBufUpdate( inPktArray, inPktCnt, outPktArray, outPktCnt );

  if( outPktArray != NULL )
    _abuf_copy_in(outPktArray,outPktCnt);


  _cmGlobalCbCnt += 1;
}

cmRC_t audio_port_test( cmCtx_t* ctx, cmApPortTestRecd* r, bool runFl )
{
  cmRC_t        rc = kOkAdRC;
  unsigned       i = 0;
  int    srateMult = 0;  
  cmRpt_t*     rpt = &ctx->rpt;
  
  cmApSample_t buf[r->bufSmpCnt];
  char         log[r->logCnt];
  unsigned    ilog[r->logCnt];
  
  r->buf        = buf;
  r->log        = log;
  r->ilog       = ilog;
  r->cbCnt      = 0;

  _cmGlobalInDevIdx = r->inDevIdx;
  _cmGlobalOutDevIdx= r->outDevIdx;

  cmRptPrintf(rpt,"%s in:%i out:%i chidx:%i chs:%i bufs=%i frm=%i rate=%f\n",runFl?"exec":"rpt",r->inDevIdx,r->outDevIdx,r->chIdx,r->chCnt,r->bufCnt,r->framesPerCycle,r->srate);

  if( cmApFileAllocate(rpt) != kOkApRC )
  {
    rc = cmErrMsg(&ctx->err, kAudioPortFileFailAdRC,"Audio port file allocation failed.");
    goto errLabel;
  }

  // allocate the non-real-time port
  if( cmApNrtAllocate(rpt) != kOkApRC )
  {
    rc = cmErrMsg(&ctx->err, kAudioPortNrtFailAdRC,"Non-real-time system allocation failed.");
    goto errLabel;
  }

  // initialize the audio device interface
  if( cmApInitialize(rpt) != kOkApRC )
  {
    rc = cmErrMsg(&ctx->err, kAudioPortFailAdRC,"Port initialize failed.\n");
    goto errLabel;
  }
  
  // report the current audio device configuration
  for(i=0; i<cmApDeviceCount(); ++i)
  {
    cmRptPrintf(rpt,"%i [in: chs=%i frames=%i] [out: chs=%i frames=%i] srate:%f %s\n",i,cmApDeviceChannelCount(i,true),cmApDeviceFramesPerCycle(i,true),cmApDeviceChannelCount(i,false),cmApDeviceFramesPerCycle(i,false),cmApDeviceSampleRate(i),cmApDeviceLabel(i));
  }
  
  // report the current audio devices using the audio port interface function
  cmApReport(rpt);

  if( runFl )
  {
    // initialize the audio buffer
    cmApBufInitialize( cmApDeviceCount(), r->meterMs );

    // setup the buffer for the output device
    cmApBufSetup( r->outDevIdx, r->srate, r->framesPerCycle, r->bufCnt, cmApDeviceChannelCount(r->outDevIdx,true), r->framesPerCycle, cmApDeviceChannelCount(r->outDevIdx,false), r->framesPerCycle, srateMult );
    
    // setup the buffer for the input device
    if( r->inDevIdx != r->outDevIdx )
      cmApBufSetup( r->inDevIdx, r->srate, r->framesPerCycle, r->bufCnt, cmApDeviceChannelCount(r->inDevIdx,true), r->framesPerCycle, cmApDeviceChannelCount(r->inDevIdx,false), r->framesPerCycle, srateMult ); 


    // setup an output device
    if(cmApDeviceSetup(r->outDevIdx,r->srate,r->framesPerCycle,_cmApPortCb2,&r) != kOkApRC )
      rc = cmErrMsg(&ctx->err,kAudioPortFailAdRC,"Out audio device setup failed.\n");
    else
      // setup an input device
      if( cmApDeviceSetup(r->inDevIdx,r->srate,r->framesPerCycle,_cmApPortCb2,&r) != kOkApRC )
        rc = cmErrMsg(&ctx->err,kAudioPortFailAdRC,"In audio device setup failed.\n");
      else
        // start the input device
        if( cmApDeviceStart(r->inDevIdx) != kOkApRC )
          rc = cmErrMsg(&ctx->err,kAudioPortFailAdRC,"In audio device start failed.\n");
        else
          // start the output device
          if( cmApDeviceStart(r->outDevIdx) != kOkApRC )
            rc = cmErrMsg(&ctx->err, kAudioPortFailAdRC,"Out audio device start failed.\n");
          else
            cmRptPrintf(rpt,"Started...");

    cmApBufEnableChannel(r->inDevIdx, -1, kEnableApFl | kInApFl );
    cmApBufEnableMeter(  r->inDevIdx, -1, kEnableApFl | kInApFl );

    cmApBufEnableChannel(r->outDevIdx, -1, kEnableApFl | kOutApFl );
    cmApBufEnableMeter(  r->outDevIdx, -1, kEnableApFl | kOutApFl );
    
    cmRptPrintf(rpt,"q=quit O/o=output tone, I/i=input tone P/p=pass s=buf report\n");
    char c;
    while((c=getchar()) != 'q')
    {
      //cmApAlsaDeviceRtReport(rpt,r->outDevIdx);

      switch(c)
      {
        case 'i':
        case 'I':
          cmApBufEnableTone(r->inDevIdx,-1,kInApFl | (c=='I'?kEnableApFl:0));
          break;

        case 'o':
        case 'O':
          cmApBufEnableTone(r->outDevIdx,-1,kOutApFl | (c=='O'?kEnableApFl:0));
          break;

        case 'p':
        case 'P':
          cmApBufEnablePass(r->outDevIdx,-1,kOutApFl | (c=='P'?kEnableApFl:0));
          break;
          
        case 's':
          cmApBufReport(rpt);
          cmRptPrintf(rpt,"CB:%i\n",_cmGlobalCbCnt);
          break;
      }

    }

    // stop the input device
    if( cmApDeviceIsStarted(r->inDevIdx) )
      if( cmApDeviceStop(r->inDevIdx) != kOkApRC )
        cmRptPrintf(rpt,"In device stop failed.\n");

    // stop the output device
    if( cmApDeviceIsStarted(r->outDevIdx) )
      if( cmApDeviceStop(r->outDevIdx) != kOkApRC )
        cmRptPrintf(rpt,"Out device stop failed.\n");
  }

 errLabel:

  // release any resources held by the audio port interface
  if( cmApFinalize() != kOkApRC )
    rc = cmErrMsg(&ctx->err,kAudioPortFailAdRC,"Finalize failed.\n");

  cmApBufFinalize();

  cmApNrtFree();
  cmApFileFree();

  // report the count of audio buffer callbacks
  cmRptPrintf(rpt,"cb count:%i\n", r->cbCnt );
  //for(i=0; i<_logCnt; ++i)
  //  cmRptPrintf(rpt,"%c(%i)",_log[i],_ilog[i]);
  //cmRptPrintf(rpt,"\n");


  return rc;  
}

void print( void* arg, const char* text )
{
  printf("%s",text);
}

int main( int argc, char* argv[] )
{
  enum
  {
   kSratePoId = kBasePoId,
   kHzPoId,
   kChIdxPoId,
   kChCntPoId,
   kBufCntPoId,
   kFrmCntPoId,
   kFrmsPerBufPoId,
   kInDevIdxPoId,
   kOutDevIdxPoId,
   kReportFlagPoId
  };
  
  cmRC_t          rc              = cmOkRC;
  bool            memDebugFl      = cmDEBUG_FL;
  unsigned        memGuardByteCnt = memDebugFl ? 8 : 0;
  unsigned        memAlignByteCnt = 16;
  unsigned        memFlags        = memDebugFl ? kTrackMmFl | kDeferFreeMmFl | kFillUninitMmFl : 0;  
  cmPgmOptH_t     poH             = cmPgmOptNullHandle;
  const cmChar_t* appTitle        = "audiodev";
  unsigned        reportFl        = 0;
  cmCtx_t         ctx;
  cmApPortTestRecd r; 
  memset(&r,0,sizeof(r));
  r.meterMs = 50;
  r.logCnt  = 100;

  memset(abuf,0,sizeof(cmApSample_t)*abufN);
  
  cmCtxSetup(&ctx,appTitle,print,print,NULL,memGuardByteCnt,memAlignByteCnt,memFlags);

  cmMdInitialize( memGuardByteCnt, memAlignByteCnt, memFlags, &ctx.rpt );

  cmFsInitialize( &ctx, appTitle );

  cmTsInitialize(&ctx );

  cmPgmOptInitialize(&ctx, &poH, poBegHelpStr, poEndHelpStr );

  cmPgmOptInstallDbl( poH, kSratePoId,           's', "srate",      0,       48000,          &r.srate,     1,
    "Audio system sample rate." );

  cmPgmOptInstallDbl( poH, kHzPoId,              'z', "hz",         0,        1000,          &r.frqHz,     1,
    "Tone frequency in Hertz." );
   
  cmPgmOptInstallUInt( poH, kChIdxPoId,          'x', "ch_index",   0,           0,          &r.chIdx,     1,
    "Index of first channel index." );

  cmPgmOptInstallUInt( poH, kChCntPoId,          'c', "ch_cnt",     0,           2,          &r.chCnt,     1,
    "Count of audio channels." );
  
  cmPgmOptInstallUInt( poH, kBufCntPoId,         'b', "buf_cnt",    0,           3,          &r.bufCnt,    1,
    "Count of audio buffers. (e.g. 2=double buffering, 3=triple buffering)" );

  cmPgmOptInstallUInt( poH, kFrmsPerBufPoId,     'f', "frames_per_buf",0,      512,   &r.framesPerCycle,   1,
    "Count of audio channels." );
  
  cmPgmOptInstallUInt( poH, kInDevIdxPoId,       'i', "in_dev_index",0,          0,          &r.inDevIdx,  1,
    "Input device index as taken from the audio device report." );

  cmPgmOptInstallUInt( poH, kOutDevIdxPoId,      'o', "out_dev_index",0,         0,          &r.outDevIdx, 1,
    "Output device index as taken from the audio device report." );
  
  cmPgmOptInstallFlag( poH, kReportFlagPoId,     'r', "report_flag",  0,         1,          &reportFl,    1,
    "Print an audio device report." );
  
    // parse the command line arguments
  if( cmPgmOptParse(poH, argc, argv ) == kOkPoRC )
  {
    // handle the built-in arg's (e.g. -v,-p,-h)
    // (returns false if only built-in options were selected)
    if( cmPgmOptHandleBuiltInActions(poH, &ctx.rpt ) == false )
      goto errLabel;

    rc = audio_port_test( &ctx, &r, !reportFl );
  }

 errLabel:
  _abuf_write_audio_file(&ctx);
  cmPgmOptFinalize(&poH);
  cmTsFinalize();
  cmFsFinalize();
  cmMdReport( kIgnoreNormalMmFl );
  cmMdFinalize();
  
  return rc;
}
