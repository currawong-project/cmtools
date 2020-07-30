#include "cmPrefix.h"
#include "cmGlobal.h"
#include "cmFloatTypes.h"
#include "cmComplexTypes.h"
#include "cmRpt.h"
#include "cmErr.h"
#include "cmCtx.h"
#include "cmMem.h"
#include "cmMallocDebug.h"
#include "cmLinkedHeap.h"
#include "cmSymTbl.h"
#include "cmFile.h"
#include "cmFileSys.h"
#include "cmTime.h"
#include "cmMidi.h"
#include "cmAudioFile.h"
#include "cmMidiFile.h"
#include "cmJson.h"
#include "cmText.h"

#include "cmProcObj.h"
#include "cmProcTemplateMain.h"
#include "cmVectOpsTemplateMain.h"
#include "cmProc.h"
#include "cmProc2.h"

#include "cmTimeLine.h"
#include "cmOnset.h"
#include "cmPgmOpts.h"
#include "cmScore.h"

typedef cmRC_t masRC_t;

enum
{
  kOkMasRC = cmOkRC,
  kFailMasRC,
  kJsonFailMasRC,
  kParamErrMasRC,
  kTimeLineFailMasRC
};

enum
{
  kMidiToAudioSelId,
  kAudioOnsetSelId,
  kConvolveSelId,
  kSyncSelId,
  kGenTimeLineSelId,
  kLoadMarkersSelId,
  kTestStubSelId
};


typedef struct
{
  const cmChar_t* input;
  const cmChar_t* output;
  unsigned        selId;
  double          wndMs;
  double          srate;
  cmOnsetCfg_t    onsetCfg;
  const cmChar_t* refDir;
  const cmChar_t* keyDir;
  const cmChar_t* refExt;
  const cmChar_t* keyExt;
  const cmChar_t* markFn;
  const cmChar_t* prefixPath;
} masPgmArgs_t;

typedef struct
{
  const char* refFn;
  double      refWndBegSecs;    // location of the ref window in the midi file
  double      refWndSecs;       // length of the ref window
  const char* keyFn;
  double      keyBegSecs;       // offset into audio file of first sliding window
  double      keyEndSecs;       // offset into audio file of the last sliding window
  unsigned    keySyncIdx;       // index into audio file of best matched sliding window
  double      syncDist;         // distance (matching score) to the ref window of the best matched sliding window
  unsigned    refSmpCnt;        // count of samples in the midi file 
  unsigned    keySmpCnt;        // count of samples in the audio file
  double      srate;            // sample rate of audio and midi file
} syncRecd_t;
// Notes:
// audioBegSecs
//  != 0  - audio file is locked to midi file  
//  == 0  - midi  file is locked to audio file 

typedef struct
{
  cmJsonH_t       jsH;
  syncRecd_t*     syncArray;
  unsigned        syncArrayCnt;
  const cmChar_t* refDir;
  const cmChar_t* keyDir;
  double          hopMs;
} syncCtx_t;

enum
{
  kMidiFl = 0x01,
  kAudioFl= 0x02,
  kLabelCharCnt = 31
};


// Notes:
// 1) Master files will have refPtr==NULL.
// 2) refSmpIdx and keySmpIdx are only valid for slave files.
// 3) A common group id is given to sets of files which are
//    locked in time relative to one another.  For example
//    if file B and C are synced to master file A and
//    file D is synced to file E which is synced to master
//    file F.  Then files A,B,C will be given one group
//    id and files D,E and F will be given another group id.
//
typedef struct file_str
{
  unsigned         flags;         // see kXXXFl 
  const char*      fn;            // file name of this file
  const char*      fullFn;        // path and name
  unsigned         refIdx;        // index into file array of recd pointed to by refPtr 
  struct file_str* refPtr;        // ptr to the file that this file is positioned relative to (set to NULL for master files)
  int              refSmpIdx;     // index into the reference file that is synced to keySmpIdx
  int              keySmpIdx;     // index into this file which is synced to refSmpIdx
  int              absSmpIdx;     // abs smp idx of sync location
  int              absBegSmpIdx;  // file beg smp idx - the earliest file in the group is set to 0.
  int              smpCnt;        // file duration
  double           srate;         // file sample rate
  unsigned         groupId;       // every file belongs to a group - a group is a set of files referencing a common master  

  char             label[ kLabelCharCnt+1 ];
  
} fileRecd_t;


void print( void* p, const cmChar_t* text)
{
  if( text != NULL )
  {
    printf("%s",text);
    fflush(stdout);
  }
}

masRC_t midiStringSearch( cmCtx_t* ctx, const cmChar_t* srcDir, cmMidiByte_t* x, unsigned xn )
{
  cmFileSysDirEntry_t* dep         = NULL;
  unsigned             dirEntryCnt = 0;
  unsigned             i,j,k;
  masRC_t              rc          = kOkMasRC;
  unsigned             totalNoteCnt = 0;

  typedef struct
  {
    cmMidiByte_t pitch;
    unsigned     micros;
  } note_t;

  assert( xn > 0 );

  note_t wnd[ xn ];

    // iterate the source directory
  if( (dep = cmFsDirEntries( srcDir, kFileFsFl | kFullPathFsFl, &dirEntryCnt )) == NULL )
    return cmErrMsg(&ctx->err,kFailMasRC,"Unable to iterate the source directory '%s'.",srcDir);

  // for each file in the source directory
  for(i=0; i<dirEntryCnt; ++i)
  {
    cmMidiFileH_t mfH;

    // open the MIDI file
    if( cmMidiFileOpen(ctx, &mfH, dep[i].name) != kOkMfRC )
    {
      rc = cmErrMsg(&ctx->err,kFailMasRC,"The MIDI file '%s' could not be opened.",dep[i].name);
      goto errLabel;
    }

    cmRptPrintf(ctx->err.rpt,"%3i of %3i %s  ",i,dirEntryCnt,dep[i].name);

    unsigned                 msgCnt    = cmMidiFileMsgCount(mfH);        // get the count of messages in the MIDI file
    const cmMidiTrackMsg_t** msgPtrPtr = cmMidiFileMsgArray(mfH);        // get a ptr to the base of the the MIDI msg array
    //cmMidiFileTickToMicros(mfH);                                       // convert the MIDI msg time base from ticks to microseconds
    
    // empty the window
    for(j=0; j<xn; ++j)
      wnd[j].pitch = kInvalidMidiPitch;

    unsigned micros  = 0;
    unsigned noteCnt = 0;
    for(k=0; k<msgCnt; ++k)
    {
      const cmMidiTrackMsg_t* mp = msgPtrPtr[k];

      micros += mp->dtick;

      if( mp->status == kNoteOnMdId )
      {
        ++noteCnt;

        // shift the window to the left
        for(j=0; j<xn-1; ++j)
          wnd[j] = wnd[j+1];

        // insert the new note on the right
        wnd[ xn-1 ].pitch  = mp->u.chMsgPtr->d0;
        wnd[ xn-1 ].micros = micros;

        // compare the window to the search string
        for(j=0; j<xn; ++j)
          if( wnd[j].pitch != x[j] )
            break;

        // if a match was found
        if( j == xn )
          cmRptPrintf(ctx->err.rpt,"\n %5i %5.1f ", i, /* minuites */ (double)micros/60000000.0 ); 
      }

    }

    totalNoteCnt += noteCnt;

    cmRptPrintf(ctx->err.rpt,"%i %i \n",noteCnt,totalNoteCnt);

    // close the midi file
    cmMidiFileClose(&mfH);

  }

 errLabel:
  cmFsDirFreeEntries(dep);

  return rc;
}


// Generate an audio file containing impulses at the location of each note-on message. 
masRC_t midiToAudio(  cmCtx_t* ctx, const cmChar_t* midiFn, const cmChar_t* audioFn, double srate )
{
  cmMidiFileH_t            mfH        = cmMidiFileNullHandle;
  unsigned                 sampleBits = 16;
  unsigned                 chCnt      = 1;

  unsigned                 msgCnt;
  const cmMidiTrackMsg_t** msgPtrPtr;
  masRC_t                  rc         = kFailMasRC;
  cmRC_t                   afRC       = kOkAfRC;
  cmAudioFileH_t           afH        = cmNullAudioFileH;
  unsigned                 bufSmpCnt  = 1024;
  cmSample_t               buf[ bufSmpCnt ];
  cmSample_t              *bufPtr     = buf;
  unsigned                 noteOnCnt  = 0;
  
  // open the MIDI file
  if( cmMidiFileOpen(ctx,&mfH,midiFn) != kOkMfRC )
    return kFailMasRC;

  // force the first event to occur one quarter note into the file
  cmMidiFileSetDelay(mfH, cmMidiFileTicksPerQN(mfH) );

  double mfDurSecs = cmMidiFileDurSecs(mfH);
  cmRptPrintf(&ctx->rpt,"Secs:%f \n",mfDurSecs);

  msgCnt    = cmMidiFileMsgCount(mfH);        // get the count of messages in the MIDI file
  msgPtrPtr = cmMidiFileMsgArray(mfH);        // get a ptr to the base of the the MIDI msg array
  //cmMidiFileTickToMicros(mfH);                // convert the MIDI msg time base from ticks to microseconds

  if( msgCnt == 0 )
  {
    rc = kOkMasRC;
    goto errLabel;
  }

  // create the output audio file
  if( cmAudioFileIsValid( afH = cmAudioFileNewCreate(audioFn, srate, sampleBits, chCnt, &afRC, &ctx->rpt))==false )
  {
    cmErrMsg(&ctx->err,kFailMasRC,"The attempt to create the audio file '%s' failed.",audioFn);
    goto errLabel;
  }

  unsigned msgIdx      = 0;
  unsigned msgSmpIdx   = floor( msgPtrPtr[msgIdx]->dtick  * srate / 1000000.0);
  unsigned begSmpIdx   = 0;

  do
  {

    // zero the audio buffer
    cmVOS_Zero(buf,bufSmpCnt);

    // for each msg which falls inside the current buffer
    while( begSmpIdx <= msgSmpIdx && msgSmpIdx < begSmpIdx + bufSmpCnt )
    {

      // put an impulse representing this note-on msg in the buffer
      if( msgPtrPtr[msgIdx]->status == kNoteOnMdId )
      {
        buf[ msgSmpIdx - begSmpIdx ] = (cmSample_t)msgPtrPtr[msgIdx]->u.chMsgPtr->d1 / 127;
        ++noteOnCnt;
      }
      
      // advance to the next msg
      ++msgIdx;

      if( msgIdx == msgCnt )
        break;

      // update the current msg time
      msgSmpIdx += floor( msgPtrPtr[msgIdx]->dtick  * srate / 1000000.0);
    }

    // write the audio buffer
    if( cmAudioFileWriteFloat(afH, bufSmpCnt, chCnt, &bufPtr ) != kOkAfRC )
    {
      cmErrMsg(&ctx->err,kFailMasRC,"Audio file write failed on '%s'.",audioFn);
      goto errLabel;
    }
    
    // advance the buffer position
    begSmpIdx += bufSmpCnt;

  }while(msgIdx < msgCnt);
  
  /*
  // for each MIDI msg
  for(i=0; i<msgCnt; ++i)
  {
    const cmMidiTrackMsg_t* mp = msgPtrPtr[i];

    // calculate the absolute time of this msg in microseconds
    absUSecs += mp->dtick; 

    // if this is a note on msg
    if( mp->status == kNoteOnMdId && absUSecs > shiftUSecs )
    {
      // convert the msg time to samples
      unsigned smpIdx = floor((absUSecs - shiftUSecs) * srate / 1000000.0);

      assert(smpIdx<smpCnt);

      // put a velocity scaled impulse in the audio file
      sV[smpIdx] = (cmSample_t)mp->u.chMsgPtr->d1 / 127;
    }
  }

  cmSample_t** bufPtrPtr = &sV;
  if( cmAudioFileWriteFileSample(audioFn,srate,sampleBits,smpCnt,chCnt,bufPtrPtr,&ctx->rpt) != kOkAfRC )
    goto errLabel;
  */

  rc = kOkMasRC;

  cmRptPrintf(&ctx->rpt,"Note-on count:%i\n",noteOnCnt);

 errLabel:

  //cmMemFree(sV);

  if( cmAudioFileIsValid(afH) )
    cmAudioFileDelete(&afH);

  // close the midi file
  cmMidiFileClose(&mfH);

  return rc;
    
}

masRC_t filter( cmCtx_t* ctx, const cmChar_t* inAudioFn, const cmChar_t* outAudioFn, double wndMs, double feedbackCoeff )
{
  cmAudioFileH_t    iafH = cmNullAudioFileH;
  cmAudioFileH_t    oafH = cmNullAudioFileH;
  masRC_t           rc   = kFailMasRC;
  cmAudioFileInfo_t afInfo;
  cmRC_t            afRC;
  double            prog = 0.1;
  unsigned          progIdx = 0;

  // open the input audio file
  if( cmAudioFileIsValid( iafH = cmAudioFileNewOpen(inAudioFn,&afInfo,&afRC, &ctx->rpt ))==false)
    return kFailMasRC;

  // create the output audio file
  if( cmAudioFileIsValid( oafH = cmAudioFileNewCreate(outAudioFn,afInfo.srate,afInfo.bits,1,&afRC,&ctx->rpt)) == false )
    goto errLabel;
  else
  {
    unsigned   wndSmpCnt  = floor(afInfo.srate * wndMs / 1000);
    unsigned   procSmpCnt = wndSmpCnt;
    cmSample_t procBuf[procSmpCnt];
    unsigned   actFrmCnt;
    cmReal_t   a[] = {-feedbackCoeff };
    cmReal_t   b0  = 1.0;
    cmReal_t   b[] = {0,};
    cmReal_t   d[] = {0,0};

    do
    {
      unsigned chIdx = 0;
      unsigned chCnt = 1;

      cmSample_t* procBufPtr = procBuf;

      actFrmCnt = 0;

      // read the next procSmpCnt samples from the input file into procBuf[]
      cmAudioFileReadSample(iafH, procSmpCnt, chIdx, chCnt, &procBufPtr, &actFrmCnt );
     
      if( actFrmCnt > 0 )
      {
        
        cmSample_t y[actFrmCnt];
        cmSample_t* yp = y;
        cmVOS_Filter( y, actFrmCnt, procBuf, actFrmCnt, b0, b, a, d, 1 );

        // write the output audio file
        if( cmAudioFileWriteSample(oafH, actFrmCnt, chCnt, &yp ) != kOkAfRC )
          goto errLabel;
      }

      progIdx += actFrmCnt;

      if( progIdx > prog * afInfo.frameCnt )
      {
        cmRptPrintf(&ctx->rpt,"%i ",(int)round(prog*10));
        prog += 0.1;
      }
      
    }while(actFrmCnt==procSmpCnt);

    cmRptPrint(&ctx->rpt,"\n");
  }
  

  rc = kOkMasRC;

 errLabel:

  if( cmAudioFileIsValid(iafH) )
    cmAudioFileDelete(&iafH);

  if( cmAudioFileIsValid(oafH) )
    cmAudioFileDelete(&oafH);

  return rc;
}


masRC_t convolve( cmCtx_t* ctx, const cmChar_t* inAudioFn, const cmChar_t* outAudioFn, double wndMs )
{
  cmAudioFileH_t    iafH = cmNullAudioFileH;
  cmAudioFileH_t    oafH = cmNullAudioFileH;
  cmCtx*            ctxp = NULL;
  cmConvolve*       cnvp = NULL;
  masRC_t           rc   = kFailMasRC;
  cmAudioFileInfo_t afInfo;
  cmRC_t            afRC;
  double            prog = 0.1;
  unsigned          progIdx = 0;

  // open the input audio file
  if( cmAudioFileIsValid( iafH = cmAudioFileNewOpen(inAudioFn,&afInfo,&afRC, &ctx->rpt ))==false)
    return kFailMasRC;

  // create the output audio file
  if( cmAudioFileIsValid( oafH = cmAudioFileNewCreate(outAudioFn,afInfo.srate,afInfo.bits,1,&afRC,&ctx->rpt)) == false )
    goto errLabel;
  else
  {
    unsigned   wndSmpCnt  = floor(afInfo.srate * wndMs / 1000);
    unsigned   procSmpCnt = wndSmpCnt;
    cmSample_t wnd[wndSmpCnt];
    cmSample_t procBuf[procSmpCnt];
    unsigned   actFrmCnt;

    cmVOS_Hann(wnd,wndSmpCnt);
    //cmVOS_DivVS(wnd,wndSmpCnt, fl ? 384 : 2);
    cmVOS_DivVS(wnd,wndSmpCnt, 4);

    ctxp = cmCtxAlloc(NULL,&ctx->rpt,cmLHeapNullHandle,cmSymTblNullHandle);  // alloc a cmCtx object
    cnvp = cmConvolveAlloc(ctxp,NULL,wnd,wndSmpCnt,procSmpCnt);              // alloc a convolver object

    do
    {
      unsigned chIdx = 0;
      unsigned chCnt = 1;

      cmSample_t* procBufPtr = procBuf;

      actFrmCnt = 0;

      // read the next procSmpCnt samples from the input file into procBuf[]
      cmAudioFileReadSample(iafH, procSmpCnt, chIdx, chCnt, &procBufPtr, &actFrmCnt );
     
      if( actFrmCnt > 0 )
      {
        // convolve the audio signal with the Gaussian window
        cmConvolveExec(cnvp,procBuf,actFrmCnt);

        //cmVOS_AddVV( cnvp->outV, cnvp->outN, procBufPtr );

        // write the output audio file
        if( cmAudioFileWriteSample(oafH, cnvp->outN, chCnt, &cnvp->outV ) != kOkAfRC )
          goto errLabel;
      }

      progIdx += actFrmCnt;

      if( progIdx > prog * afInfo.frameCnt )
      {
        cmRptPrintf(&ctx->rpt,"%i ",(int)round(prog*10));
        prog += 0.1;
      }
      
    }while(actFrmCnt==procSmpCnt);

    cmRptPrint(&ctx->rpt,"\n");
  }
  

  rc = kOkMasRC;

 errLabel:
    cmCtxFree(&ctxp);
    cmConvolveFree(&cnvp);

  if( cmAudioFileIsValid(iafH) )
    cmAudioFileDelete(&iafH);

  if( cmAudioFileIsValid(oafH) )
    cmAudioFileDelete(&oafH);

  return rc;
}

masRC_t audioToOnset( cmCtx_t* ctx, const cmChar_t* ifn, const cmChar_t* ofn, const cmOnsetCfg_t* cfg )
{
  masRC_t              rc   = kOkMasRC;
  cmOnH_t              onH  = cmOnsetNullHandle;
  cmFileSysPathPart_t* ofsp = NULL;
  const cmChar_t*      tfn  = NULL;

  // parse the output file name
  if((ofsp = cmFsPathParts(ofn)) == NULL )
  {
    rc =  cmErrMsg(&ctx->err,kFailMasRC,"Onset detector output file name '%s' could not be parsed.",cmStringNullGuard(ofn));
    goto errLabel;
  }

  // verify the output audio file does not use the 'txt' extension
  if(strcmp(ofsp->extStr,"txt") == 0 )
  {
    rc = cmErrMsg(&ctx->err,kFailMasRC,"The onset detector output audio file name cannot use the file name extension 'txt' because it will class with the output text file name.");
    goto errLabel;
  }

  // generate the output text file name by setting the output audio file name to '.txt'.
  if((tfn = cmFsMakeFn(ofsp->dirStr,ofsp->fnStr,"txt",NULL)) == NULL )
  {
    rc = cmErrMsg(&ctx->err,kFailMasRC,"Onset detector output file name generation failed on %s.",cmStringNullGuard(ifn));
    goto errLabel;
  }

  // initialize the onset detection API
  if( cmOnsetInitialize(ctx,&onH) != kOkOnRC )
  {
    rc = cmErrMsg(&ctx->err,kFailMasRC,"The onset detector initialization failed on %s.",cmStringNullGuard(ifn));
    goto errLabel;
  }

  // run the onset detector
  if( cmOnsetProc( onH, cfg, ifn ) != kOkOnRC )
  {
    rc = cmErrMsg(&ctx->err,kFailMasRC,"The onset detector execution failed on %s.",cmStringNullGuard(ifn));
    goto errLabel;
  }

  // store the results of the onset detection
  if( cmOnsetWrite( onH, ofn, tfn) != kOkOnRC )
  {
    rc = cmErrMsg(&ctx->err,kFailMasRC,"The onset detector write result failed on %s.",cmStringNullGuard(ifn));
    goto errLabel;
  }

 errLabel:
  // finalize the onset detector API
  if( cmOnsetFinalize(&onH) != kOkOnRC )
    rc = cmErrMsg(&ctx->err,kFailMasRC,"The onset detector finalization failed on %s.",cmStringNullGuard(ifn));

  cmFsFreeFn(tfn);
  cmFsFreePathParts(ofsp);

  return rc;
}

typedef struct
{
  const char* fn;
  unsigned    startSmpIdx;
  unsigned    durSmpCnt;
} audioFileRecd_t;

int compareAudioFileRecds( const void* p0, const void* p1 )
{ return strcmp(((const audioFileRecd_t*)p0)->fn,((const audioFileRecd_t*)p1)->fn); }

// Print out information on all audio files in a directory.
masRC_t audioFileStartTimes( cmCtx_t* ctx, const char* dirStr )
{
  cmFileSysDirEntry_t* dep         = NULL;
  unsigned             dirEntryCnt = 0;
  unsigned             i,n;
  masRC_t              rc          = kOkMasRC;

  if( (dep = cmFsDirEntries( dirStr, kFileFsFl | kFullPathFsFl, &dirEntryCnt )) == NULL )
    return kFailMasRC;
  else
  {
    audioFileRecd_t afArray[ dirEntryCnt ];

    memset(afArray,0,sizeof(afArray));

    for(i=0,n=0; i<dirEntryCnt; ++i)
    {
      cmAudioFileInfo_t afInfo;
    
      if( cmAudioFileGetInfo( dep[i].name, &afInfo, &ctx->rpt ) == kOkAfRC )
      {

        afArray[n].fn          = dep[i].name;
        afArray[n].durSmpCnt   = afInfo.frameCnt;
        afArray[n].startSmpIdx = afInfo.bextRecd.timeRefLow;
        ++n;
      }

    }

    qsort(afArray,n,sizeof(audioFileRecd_t),compareAudioFileRecds);

    for(i=0; i<n; ++i)
    {
      printf("%10i %10i %10i %s\n", afArray[i].startSmpIdx, afArray[i].durSmpCnt, afArray[i].startSmpIdx + afArray[i].durSmpCnt, afArray[i].fn );
    }

    cmFsDirFreeEntries(dep);

    
  }
  return rc;
}



// srate - only used when sel == kMidiToAudioSelId
// wndMs - only used when sel == kConvolveSelId
// onsetCfgPtr - only used when sel == kAudioOnsetSelId
masRC_t fileDriver( cmCtx_t* ctx, unsigned sel, const cmChar_t* srcDir, const cmChar_t* dstDir, double srate, double wndMs, const cmOnsetCfg_t* onsetCfgPtr )
{
  cmFileSysDirEntry_t* dep         = NULL;
  unsigned             dirEntryCnt = 0;
  unsigned             i;
  masRC_t              rc          = kOkMasRC;

  // verify / create the destination directory
  if( !cmFsIsDir(dstDir) )
    if( cmFsMkDir(dstDir) != kOkFsRC )
      return cmErrMsg(&ctx->err,kFailMasRC,"Attempt to make directory the directory '%s' failed.",dstDir);

  // iterate the source directory
  if( (dep = cmFsDirEntries( srcDir, kFileFsFl | kFullPathFsFl, &dirEntryCnt )) == NULL )
    return cmErrMsg(&ctx->err,kFailMasRC,"Unable to iterate the source directory '%s'.",srcDir);
  else
  {
    // for each file in the source directory
    for(i=0; i<dirEntryCnt; ++i)
    {
      // parse the file name
      cmFileSysPathPart_t* pp = cmFsPathParts( dep[i].name );

      // combine the dstDir and source file name to form the dest. file name
      const cmChar_t* dstFn = cmFsMakeFn( dstDir, pp->fnStr, "aif", NULL );

      cmRptPrintf(&ctx->rpt,"Source File:%s\n", dep[i].name);

      switch( sel )
      {
        case kMidiToAudioSelId:
          // convert the MIDI to an audio impulse file
          if( midiToAudio(ctx, dep[i].name, dstFn, srate ) != kOkMasRC )
            cmErrMsg(&ctx->err,kFailMasRC,"MIDI to audio failed.");
          break;

        case kConvolveSelId:
          // convolve impulse audio file with Hann window
          if( convolve(ctx,dep[i].name, dstFn, wndMs ) != kOkMasRC )
            cmErrMsg(&ctx->err,kFailMasRC,"Convolution failed.");
          break;

        case kAudioOnsetSelId:
          if( audioToOnset(ctx,dep[i].name, dstFn, onsetCfgPtr ) )
            cmErrMsg(&ctx->err,kFailMasRC,"Audio to onset failed.");
          break;
      }

      cmFsFreeFn(dstFn);
      
      cmFsFreePathParts(pp);
    }

    cmFsDirFreeEntries(dep);
  }
  
  return rc;
}


// b0 = base of window to compare.
// b0[i] = location of sample in b0[] to compare to b1[0].
// b1[n] = reference window
double distance( const cmSample_t* b0, const cmSample_t* b1, unsigned n, double maxDist )
{
  double            sum = 0;
  const cmSample_t* ep  = b1 + n;
  
  while(b1 < ep && sum < maxDist )
  {
    sum += ((*b0)-(*b1)) * ((*b0)-(*b1));
    ++b0;
    ++b1;
  }
  return sum;
}


// write a syncCtx_t record as a JSON file
masRC_t write_sync_json( cmCtx_t* ctx, const syncCtx_t* scp, const cmChar_t* outJsFn )
{
  masRC_t       rc  = kOkMasRC;
  unsigned      i;
  cmJsonH_t     jsH = cmJsonNullHandle;
  cmJsonNode_t* jnp;

  // create a JSON tree
  if( cmJsonInitialize(&jsH,ctx) != kOkJsRC )
  {
    rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"JSON output handle initialization failed on '%s'.",cmStringNullGuard(outJsFn));
    goto errLabel;
  }

  // create an outer container object
  if((jnp = cmJsonCreateObject(jsH,NULL)) == NULL )
    goto errLabel;

  // create the 'sync' object
  if((jnp = cmJsonInsertPairObject(jsH,jnp,"sync")) == NULL )
    goto errLabel;

  if( cmJsonInsertPairs(jsH,jnp,
      "refDir",kStringTId,scp->refDir,
      "keyDir",kStringTId,scp->keyDir,
      "hopMs", kRealTId, scp->hopMs,
      NULL) != kOkJsRC )
  {
    goto errLabel;
  }

  if((jnp = cmJsonInsertPairArray(jsH,jnp,"array")) == NULL )
    goto errLabel;

  for(i=0; i<scp->syncArrayCnt; ++i)
  {
    const syncRecd_t* s = scp->syncArray + i;

    if( cmJsonCreateFilledObject(jsH,jnp, 
        "refFn",        kStringTId, s->refFn,
        "refWndBegSecs",kRealTId,   s->refWndBegSecs,
        "refWndSecs",   kRealTId,   s->refWndSecs,
        "keyFn",        kStringTId, s->keyFn,
        "keyBegSecs",   kRealTId,   s->keyBegSecs,
        "keyEndSecs",   kRealTId,   s->keyEndSecs,
        "keySyncIdx",   kIntTId,    s->keySyncIdx,
        "syncDist",     kRealTId,   s->syncDist,
        "refSmpCnt",    kIntTId,    s->refSmpCnt,
        "keySmpCnt",    kIntTId,    s->keySmpCnt,
        "srate",        kRealTId,   s->srate,
        NULL) == NULL )
    {
      goto errLabel;
    }
  }

 errLabel:
  if( cmJsonErrorCode(jsH) != kOkJsRC )
    rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"JSON tree construction failed on '%s'.",cmStringNullGuard(outJsFn));
  else
  {
    if( cmJsonWrite(jsH,cmJsonRoot(jsH),outJsFn) != kOkJsRC )
      rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"JSON write failed on '%s.",cmStringNullGuard(outJsFn));
  }
  
  if( cmJsonFinalize(&jsH) != kOkJsRC )
    rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"JSON output finalization failed on '%s'.",cmStringNullGuard(outJsFn));

  
  return rc;
}

masRC_t _masJsonFieldNotFoundError( cmCtx_t* c, const char* msg, const char* errLabelPtr, const char* cfgFn )
{
  masRC_t rc;

  if( errLabelPtr != NULL )
    rc = cmErrMsg( &c->err, kJsonFailMasRC, "Cfg. %s field not found:'%s' in file:'%s'.",msg,cmStringNullGuard(errLabelPtr),cmStringNullGuard(cfgFn));
  else
    rc = cmErrMsg( &c->err, kJsonFailMasRC, "Cfg. %s parse failed '%s'.",msg,cmStringNullGuard(cfgFn) );

  return rc;
}

// Initialize a syncCtx_t record from a JSON file.
masRC_t read_sync_json( cmCtx_t* ctx, syncCtx_t* scp, const cmChar_t* jsFn )
{
  masRC_t         rc          = kOkMasRC;
  cmJsonNode_t*   jnp;
  const cmChar_t* errLabelPtr = NULL;
  unsigned        i;

  // if the JSON tree already exists then finalize it
  if( cmJsonFinalize(&scp->jsH) != kOkJsRC )
    return cmErrMsg(&ctx->err,kJsonFailMasRC,"JSON object finalization failed.");
  
  // initialize a JSON tree from a file
  if( cmJsonInitializeFromFile(&scp->jsH, jsFn, ctx ) != kOkJsRC )
  {
    rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"Initializatoin from JSON file failed on '%s'.",cmStringNullGuard(jsFn));
    goto errLabel;
  }

  // find the 'sync' object
  if((jnp = cmJsonFindValue(scp->jsH,"sync",cmJsonRoot(scp->jsH),kObjectTId)) == NULL )
  {
    rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"This JSON file does not have a 'sync' object.");
    goto errLabel;
  }

  // read the 'sync' object header
  if( cmJsonMemberValues( jnp, &errLabelPtr,
      "refDir", kStringTId, &scp->refDir,
      "keyDir", kStringTId, &scp->keyDir,
      "hopMs",  kRealTId,   &scp->hopMs,
      "array",  kArrayTId,  &jnp,
      NULL ) != kOkJsRC )
  {
    rc = _masJsonFieldNotFoundError(ctx, "sync", errLabelPtr, jsFn );
    goto errLabel;
  }

  // allocate the array to hold the sync array records
  if((scp->syncArrayCnt = cmJsonChildCount(jnp)) > 0 )
    scp->syncArray = cmMemResizeZ(syncRecd_t,scp->syncArray,scp->syncArrayCnt);

  // read each sync recd
  for(i=0; i<scp->syncArrayCnt; ++i)
  {
    const cmJsonNode_t* cnp = cmJsonArrayElementC(jnp,i);
    syncRecd_t*         s   = scp->syncArray + i;

    if( cmJsonMemberValues(cnp, &errLabelPtr,
        "refFn",        kStringTId, &s->refFn,
        "refWndBegSecs",kRealTId,   &s->refWndBegSecs,
        "refWndSecs",   kRealTId,   &s->refWndSecs,
        "keyFn",        kStringTId, &s->keyFn,
        "keyBegSecs",   kRealTId,   &s->keyBegSecs,
        "keyEndSecs",   kRealTId,   &s->keyEndSecs,
        "keySyncIdx",   kIntTId,    &s->keySyncIdx,
        "syncDist",     kRealTId,   &s->syncDist,
        "refSmpCnt",    kIntTId,    &s->refSmpCnt,
        "keySmpCnt",    kIntTId,    &s->keySmpCnt,
        "srate",        kRealTId,   &s->srate,
        NULL) != kOkJsRC )
    {
      rc = _masJsonFieldNotFoundError(ctx, "sync record", errLabelPtr, jsFn );
      goto errLabel;
    }
  }

 errLabel:

  if( rc != kOkMasRC )
  {
    if( cmJsonFinalize(&scp->jsH) != kOkJsRC )
      rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"JSON finalization failed.");

    cmMemPtrFree(&scp->syncArray);
  }
  return rc;
}

// Form a reference window from file 0 at refBegMs:refBegMs + wndMs.
// Compare each wndMs window in file 1 to this window and 
// record the closest match.
// Notes:
// fn0 = midi file
// fn1 = audio file
masRC_t slide_match( cmCtx_t* ctx, const cmChar_t* fn0, const cmChar_t* fn1, syncRecd_t* s, unsigned hopMs, unsigned keyEndMs )
{
  masRC_t            rc        = kOkMasRC;
  cmAudioFileInfo_t  afInfo0;
  cmAudioFileInfo_t  afInfo1;
  cmRC_t             afRC;
  unsigned           wndMs     = s->refWndSecs    * 1000;
  unsigned           refBegMs  = s->refWndBegSecs * 1000; 
  unsigned           keyBegMs  = s->keyBegSecs   * 1000;
  cmAudioFileH_t     af0H      = cmNullAudioFileH;
  cmAudioFileH_t     af1H      = cmNullAudioFileH;
  cmSample_t        *buf0      = NULL;
  cmSample_t        *buf1      = NULL;
  unsigned           minSmpIdx = cmInvalidIdx;
  double             minDist   = DBL_MAX;

  if( cmAudioFileIsValid( af0H = cmAudioFileNewOpen(fn0,&afInfo0,&afRC, &ctx->rpt ))==false)
    return cmErrMsg(&ctx->err,kFailMasRC,"The ref. audio file could not be opened.",cmStringNullGuard(fn0));

  if( cmAudioFileIsValid( af1H = cmAudioFileNewOpen(fn1,&afInfo1,&afRC, &ctx->rpt ))==false)
  {
    rc =  cmErrMsg(&ctx->err,kFailMasRC,"The key audio file could not be opened.",cmStringNullGuard(fn1));
    goto errLabel;
  }

  assert( afInfo0.srate == afInfo1.srate );

  unsigned chCnt          = 1;
  unsigned chIdx          = 0;
  unsigned actFrmCnt      = 0;
  unsigned wndSmpCnt      = floor(wndMs * afInfo0.srate / 1000);
  unsigned hopSmpCnt      = floor(hopMs * afInfo0.srate / 1000);
  unsigned smpIdx         = 0;
  double   progIdx        = 0.01;
  unsigned keyBegSmpIdx = floor(keyBegMs * afInfo1.srate / 1000);
  unsigned keyEndSmpIdx = floor(keyEndMs * afInfo1.srate / 1000);
  unsigned hopCnt       = keyEndSmpIdx==0 ? afInfo1.frameCnt / hopSmpCnt : (keyEndSmpIdx-keyBegSmpIdx) / hopSmpCnt;

  // make wndSmpCnt an even multiple of hopSmpCnt
  wndSmpCnt = (wndSmpCnt/hopSmpCnt) * hopSmpCnt;

  if( refBegMs != 0 )
    smpIdx = floor(refBegMs * afInfo0.srate / 1000);
  else
  {
    if( afInfo0.frameCnt >= wndSmpCnt )
      smpIdx = floor(afInfo0.frameCnt / 2 - wndSmpCnt/2);
    else
    {
      wndSmpCnt = afInfo0.frameCnt;
      smpIdx    = 0;
    }
  }

  printf("wnd:%i hop:%i cnt:%i ref:%i\n",wndSmpCnt,hopSmpCnt,hopCnt,smpIdx);


  // seek to the location of the reference window
  if( cmAudioFileSeek( af0H, smpIdx ) != kOkAfRC )
  {
    rc = cmErrMsg(&ctx->err,kFailMasRC,"File seek failed while moving to ref. window in '%s'.",cmStringNullGuard(fn0));
    goto errLabel;
  }

  // take the center of file 1 as the key window
  if( cmAudioFileSeek( af1H, keyBegSmpIdx ) != kOkAfRC )
  {
    rc = cmErrMsg(&ctx->err,kFailMasRC,"File seek failed while moving to search begin location in '%s'.",cmStringNullGuard(fn1));
    goto errLabel;
  }

  // allocate the window buffers
  buf0 = cmMemAllocZ(cmSample_t,wndSmpCnt); // reference window
  buf1 = cmMemAllocZ(cmSample_t,wndSmpCnt); // sliding window

  cmSample_t* bp0 = buf0;
  cmSample_t* bp1 = buf1;

  // fill the reference window - the other buffer will be compared to this widow
  if( cmAudioFileReadSample(af0H, wndSmpCnt, chIdx, chCnt, &bp0, &actFrmCnt ) != kOkAfRC )
  {
    rc = cmErrMsg(&ctx->err,kFailMasRC,"Audio file read failed while reading the ref. window in '%s'.",cmStringNullGuard(fn0));
    goto errLabel;
  }

  // fill all except the last hopSmpCnt samples in the sliding window
  if( cmAudioFileReadSample(af1H, wndSmpCnt-hopSmpCnt, chIdx, chCnt, &bp1, &actFrmCnt ) != kOkAfRC )
  {
    rc = cmErrMsg(&ctx->err,kFailMasRC,"Audio file read failed while making the first search area read in '%s'.",cmStringNullGuard(fn1));
    goto errLabel;
  }

  smpIdx    = keyBegSmpIdx;
  bp1       = buf1 + (wndSmpCnt - hopSmpCnt);
  minSmpIdx = smpIdx;

  unsigned i         = 0;

  do
  {
    // read the new samples into the last hopSmpCnt ele's of the sliding buffer 
    if( cmAudioFileReadSample(af1H, hopSmpCnt, chIdx, chCnt, &bp1, &actFrmCnt ) != kOkAfRC )
      break;

    // compare the sliding window to the ref. window
    double dist = distance(buf1,buf0,wndSmpCnt,minDist+1);

    // record the min dist
    if( dist < minDist )
    {
      //printf("%i %f %f %f\n",minSmpIdx,minDist,dist,minDist-dist);
      minSmpIdx = smpIdx;
      minDist   = dist;
    }

    smpIdx += hopSmpCnt;

    // shift off the expired samples
    memmove(buf1, buf1 + hopSmpCnt, (wndSmpCnt-hopSmpCnt)*sizeof(cmSample_t));
        
    ++i;

    if( i > progIdx*hopCnt  )
    {
      printf("%i ",(int)(round(progIdx*100)));
      fflush(stdout);
      progIdx += 0.01;
    }

    
  }while(i<hopCnt && actFrmCnt == hopSmpCnt && (keyEndSmpIdx==0 || smpIdx < keyEndSmpIdx) );

 errLabel:

  if( 0 )
  {
      cmCtx*          ctxp = cmCtxAlloc(NULL,&ctx->rpt,cmLHeapNullHandle,cmSymTblNullHandle);  // alloc a cmCtx object
      cmBinMtxFile_t* bf0p = cmBinMtxFileAlloc(ctxp,NULL,"/home/kevin/temp/bf0.bin");
      cmBinMtxFile_t* bf1p = cmBinMtxFileAlloc(ctxp,NULL,"/home/kevin/temp/bf1.bin");

      if( cmAudioFileSeek( af1H, minSmpIdx ) != kOkAfRC )
        goto errLabel;

      bp1 = buf1;
      if( cmAudioFileReadSample(af1H, wndSmpCnt, chIdx, chCnt, &bp1, &actFrmCnt ) != kOkAfRC )
        goto errLabel;


      cmBinMtxFileExecS(bf1p,buf1,wndSmpCnt);
      cmBinMtxFileExecS(bf0p,buf0,wndSmpCnt);
      cmBinMtxFileFree(&bf0p);
      cmBinMtxFileFree(&bf1p);
      cmCtxFree(&ctxp);
  }

  cmMemPtrFree(&buf0);
  cmMemPtrFree(&buf1);
  cmAudioFileDelete(&af0H);
  cmAudioFileDelete(&af1H);

  s->syncDist    = minDist;
  s->keySyncIdx  = minSmpIdx;
  s->refSmpCnt   = afInfo0.frameCnt;
  s->keySmpCnt   = afInfo1.frameCnt;
  s->srate       = afInfo1.srate;
  return rc;
}


//
// {
//  sync_array:
//  {
//    { <ref_fn> <wnd_beg_secs> <wnd_dur_secs> <key_fn> <key_beg_secs> }
//  }
// }
masRC_t parse_sync_cfg_file( cmCtx_t* c, const cmChar_t* fn, syncCtx_t* scp )
{
  masRC_t       rc          = kOkMasRC;
  cmJsonNode_t* arr         = NULL;
  const char*   errLabelPtr = NULL;
  unsigned      i,j;

  if( cmJsonInitializeFromFile( &scp->jsH, fn, c ) != kOkJsRC )
  {
    rc = cmErrMsg(&c->err,kJsonFailMasRC,"JSON file open failed on '%s'.",cmStringNullGuard(fn));
    goto errLabel;
  }

  if( cmJsonMemberValues( cmJsonRoot(scp->jsH), &errLabelPtr,
      "ref_dir",    kStringTId, &scp->refDir,
      "key_dir",    kStringTId, &scp->keyDir,
      "hop_ms",     kRealTId,   &scp->hopMs,
      "sync_array", kArrayTId,  &arr,
      NULL ) != kOkJsRC )
  {
    rc = _masJsonFieldNotFoundError(c, "header", errLabelPtr, fn );
    goto errLabel;
  }

  if((scp->syncArrayCnt = cmJsonChildCount(arr)) == 0 )
    goto errLabel;

  scp->syncArray = cmMemAllocZ(syncRecd_t,scp->syncArrayCnt);

  for(i=0; i<cmJsonChildCount(arr); ++i)
  {
    cmJsonNode_t*   ele        = cmJsonArrayElement(arr,i);
    const cmChar_t* refFn      = NULL;
    const cmChar_t* keyFn      = NULL;
    double          wndBegSecs = 0;
    double          wndDurSecs = 0;
    double          keyBegSecs = 0;
    double          keyEndSecs = 0;
    cmJsRC_t        jsRC       = kOkJsRC;
    const int       kSix       = 6;

    if( cmJsonIsArray(ele) == false || cmJsonChildCount(ele) != kSix )
    {
      rc = cmErrMsg(&c->err,kJsonFailMasRC,"A 'sync_array' element record at index %i is not a 6 element array in '%s'.",i,fn);
      goto errLabel;      
    }

    for(j=0; j<kSix; ++j)
    {
      switch(j)
      {
        case 0: jsRC = cmJsonStringValue( cmJsonArrayElement(ele,j), &refFn );     break;
        case 1: jsRC = cmJsonRealValue(   cmJsonArrayElement(ele,j), &wndBegSecs); break;
        case 2: jsRC = cmJsonRealValue(   cmJsonArrayElement(ele,j), &wndDurSecs); break;
        case 3: jsRC = cmJsonStringValue( cmJsonArrayElement(ele,j), &keyFn );     break;
        case 4: jsRC = cmJsonRealValue(   cmJsonArrayElement(ele,j), &keyBegSecs); break;
        case 5: jsRC = cmJsonRealValue(   cmJsonArrayElement(ele,j), &keyEndSecs); break;
        default:
          {
            rc = cmErrMsg(&c->err,kJsonFailMasRC,"The 'sync_array' element record contains too many fields on record index %i in '%s'.",i,fn);
            goto errLabel;
          }
      }

      if( jsRC != kOkJsRC )
      {
        rc = cmErrMsg(&c->err,kJsonFailMasRC,"The 'sync_array' element record at index %i at field index %i in '%s'.",i,j,fn);
        goto errLabel;
      }
    }

    scp->syncArray[i].refFn         = refFn;
    scp->syncArray[i].refWndBegSecs = wndBegSecs;
    scp->syncArray[i].refWndSecs    = wndDurSecs;
    scp->syncArray[i].keyFn        = keyFn;
    scp->syncArray[i].keyBegSecs   = keyBegSecs;
    scp->syncArray[i].keyEndSecs   = keyEndSecs;

    //printf("beg:%f dur:%f ref:%s key:%s key beg:%f\n",wndBegSecs,wndDurSecs,refFn,keyFn,keyBegSecs);
  }
  
 errLabel:

  if( rc != kOkMasRC )
  {
    cmJsonFinalize(&scp->jsH);
    cmMemPtrFree(&scp->syncArray);
  }

  return rc;
}



unsigned findFile( const char* fn, unsigned flags, fileRecd_t* array, unsigned fcnt )
{
  unsigned j;

  for(j=0; j<fcnt; ++j)
  {
    if( cmIsFlag(array[j].flags,flags) && (strcmp(array[j].fn,fn)==0) )
      return  j;
  }

  return -1;
}


unsigned insertFile( const char* fn, const char* fullFn, unsigned flags, unsigned smpCnt, double srate, fileRecd_t* array, unsigned fcnt )
{
 
  if( findFile(fn,flags,array,fcnt)==-1 )
  {
    array[fcnt].fn           = fn;
    array[fcnt].fullFn       = fullFn;
    array[fcnt].flags        = flags;    
    array[fcnt].refIdx       = -1;
    array[fcnt].refPtr       = NULL;
    array[fcnt].refSmpIdx    = -1;
    array[fcnt].keySmpIdx    = -1;
    array[fcnt].absSmpIdx    = -1;
    array[fcnt].absBegSmpIdx = -1;
    array[fcnt].smpCnt       = smpCnt;
    array[fcnt].srate        = srate;
    ++fcnt;
  }

  return fcnt;
}

// calculate the absolute sample index (relative to the master file) of keySmpIdx
int calcAbsSmpIdx( const fileRecd_t* f )
{
  // if this file has no master then the absSmpIdx is 0
  if( f->refPtr == NULL )
    return 0;

  // if the reference is a master then f->refSmpIdx is also f->absSmpIdx
  if( f->refPtr->refPtr == NULL )
    return f->refSmpIdx;
  
  // this file has a master - recurse
  int v = calcAbsSmpIdx( f->refPtr );

  // absSmpIdx is the absSmpIdx of the reference plus the difference to this sync point
  // Note that both f->refSmpIdx and f->refPtr->keySmpIdx are both relative to the file pointed to by f->refPtr
  return v + (f->refSmpIdx - f->refPtr->keySmpIdx);
}

// Write an array of fileRecd_t[] (which was created from the output of sync_files()) to
// a JSON file which can be read by cmTimeLineReadJson().
masRC_t masWriteJsonTimeLine(
  cmCtx_t*    ctx, 
  double      srate, 
  fileRecd_t* fileArray, 
  unsigned    fcnt, 
  const char* outFn )
{
  masRC_t rc = kJsonFailMasRC;
  unsigned i;
  cmJsonH_t jsH = cmJsonNullHandle;
  cmJsonNode_t* jnp;

  // create JSON tree
  if( cmJsonInitialize(&jsH, ctx ) != kOkJsRC )
  {
    cmErrMsg(&ctx->err,kJsonFailMasRC,"JSON time_line output tree initialization failed.");
    goto errLabel;
  }

  // create JSON root object
  if((jnp = cmJsonCreateObject(jsH,NULL )) == NULL )
  {
    cmErrMsg(&ctx->err,kJsonFailMasRC,"JSON time_line output tree root object create failed.");
    goto errLabel;
  }

  // create the 'time_line' object
  if((jnp = cmJsonInsertPairObject(jsH,jnp,"time_line")) == NULL )
    goto errLabel;

  if( cmJsonInsertPairs(jsH,jnp,
      "srate",kRealTId,srate,
      NULL) != kOkJsRC )
  {
    goto errLabel;
  }

  if((jnp = cmJsonInsertPairArray(jsH,jnp,"objArray")) == NULL )
    goto errLabel;


  for(i=0; i<fcnt; ++i)
  {
    const fileRecd_t* f = fileArray + i;

    const cmChar_t* typeLabel   = cmIsFlag(f->flags,kAudioFl) ? "af" : "mf";
    const cmChar_t* refLabel    = f->refPtr == NULL ? "" : f->refPtr->label;
    //int             childOffset = f->refPtr == NULL ? 0  : f->absBegSmpIdx - f->refPtr->absBegSmpIdx;

    if( cmJsonCreateFilledObject(jsH,jnp, 
        "label",kStringTId,f->label,
        "type", kStringTId,typeLabel,
        "ref",  kStringTId,refLabel,
        "offset",kIntTId,f->absBegSmpIdx,
        "smpCnt",kIntTId,f->smpCnt,
        "trackId",kIntTId,f->groupId,
        "textStr",kStringTId,f->fullFn,
        NULL) == NULL )
    {
      goto errLabel;
    }
  }

  if( cmJsonWrite(jsH,cmJsonRoot(jsH),outFn) != kOkJsRC )
    goto errLabel;

  rc = kOkMasRC;
 errLabel:

  if( cmJsonFinalize(&jsH) != kOkJsRC || rc == kJsonFailMasRC )
  {
    rc = cmErrMsg(&ctx->err,rc,"JSON fail while creating time_line file.");
  }
  
  return rc;
}

const cmChar_t* _masGenTlFileName( const cmChar_t* dir, const cmChar_t* fn, const cmChar_t* ext )
{
  cmFileSysPathPart_t* pp = cmFsPathParts(fn);

  if( pp == NULL )
    return cmFsMakeFn(dir,fn,NULL,NULL);

  fn = cmFsMakeFn(dir,pp->fnStr,ext==NULL?pp->extStr:ext,NULL);

  cmFsFreePathParts(pp);

  return fn;
}

enum
{
  kSequenceGroupsMasFl = 0x01,
  kMakeOneGroupMasFl   = 0x02
};

//
// Make adjustments to fileArray[].
//
// If kSequenceGroupsMasFl is set then adjust the groups to be sequential in time by
// separating them with 'secsBetweenGroups'.
//
// If kMakeOneGroupMasFl is set then the time line object track id is set to 0 for all objects.
//
void  masProcFileArray(
  fileRecd_t* fileArray, 
  unsigned    fcnt,
  unsigned    smpsBetweenGroups,
  unsigned    flags
  )
{
  unsigned groupCnt = 0;
  unsigned groupId   = cmInvalidId;
  unsigned i,j;

  // determine the count of groups
  for(i=0; i<fcnt; ++i)
    if( fileArray[i].groupId != groupId )
    {
      ++groupCnt;
      groupId = fileArray[i].groupId;
    }

  /*
  // Set all groups to begin at time zero.
  if( cmIsFlag(flags,kZeroBaseTimeMasFl) )
  {
    for(i=0; i<groupCnt; ++i)
    {
      int minBegSmpIdx = cmInvalidIdx;

      // locate the file in this group with the earliest start time
      for(j=0; j<fcnt; ++j)
        if( fileArray[j].groupId == i )
        {
          if( minBegSmpIdx == cmInvalidIdx || fileArray[j].absBegSmpIdx < minBegSmpIdx )
            minBegSmpIdx = fileArray[j].absBegSmpIdx;
        }

      // offset all files in this group so that the earliest file starts at 0
      for(j=0; j<fcnt; ++j)
        if( fileArray[j].groupId == i )
        {
          printf("%i %i ",fileArray[j].groupId,fileArray[j].absBegSmpIdx);
          fileArray[j].absBegSmpIdx -= minBegSmpIdx;      
          printf("%i\n", fileArray[j].absBegSmpIdx);
        }
    }
  }
  */

  // Shift all groups to be seperated by secsBetweenGroups.
  if( cmIsFlag(flags,kSequenceGroupsMasFl) )
  {
    int offsetSmpCnt = 0;

    for(i=0; i<groupCnt; ++i)
    {
      int maxEndSmpIdx = 0;

      for(j=0; j<fcnt; ++j)
        if( fileArray[j].groupId == i )
        {
          
          if( fileArray[j].absBegSmpIdx + fileArray[j].smpCnt > maxEndSmpIdx )
            maxEndSmpIdx = fileArray[j].absBegSmpIdx + fileArray[j].smpCnt;

          if( fileArray[j].refPtr == NULL )
            fileArray[j].absBegSmpIdx = offsetSmpCnt;

        }

      offsetSmpCnt += maxEndSmpIdx + smpsBetweenGroups;
    }
  }

  // merge all groups into one group
  if( cmIsFlag(flags,kMakeOneGroupMasFl ) )
  {
    for(j=0; j<fcnt; ++j)
      fileArray[j].groupId = 0;
  }

}


masRC_t masCreateTimeLine( 
  cmCtx_t* ctx, 
  const syncCtx_t* scp, 
  const cmChar_t* outFn, 
  const cmChar_t* refDir, 
  const cmChar_t* keyDir,
  const cmChar_t* refExt,
  const cmChar_t* keyExt,
  double secsBetweenGroups,
  unsigned procFlags)
{
  if( scp->syncArrayCnt == 0 )
    return kOkMasRC;

  masRC_t    rc   = kOkMasRC;
  unsigned   i;
  unsigned   gcnt = 0;
  unsigned   fcnt = 0;
  fileRecd_t fileArray[2*scp->syncArrayCnt];
  
  // fill in the file array
  for(i=0; i<scp->syncArrayCnt; ++i)
  {
    const syncRecd_t* s = scp->syncArray + i;
    //printf("beg:%f sync:%i dist:%f ref:%s key:%s \n",s->keyBegSecs,s->keySyncIdx,s->syncDist,s->refFn,s->keyFn);

    // insert the reference (master) file prior to the dependent (slave) file
    const char* fn0 =  s->keyBegSecs == 0 ? s->refFn     : s->keyFn;
    const char* fn1 =  s->keyBegSecs == 0 ? s->keyFn     : s->refFn;
    unsigned    fl0 =  s->keyBegSecs == 0 ? kMidiFl      : kAudioFl;
    unsigned    fl1 =  s->keyBegSecs == 0 ? kAudioFl     : kMidiFl;
    unsigned    sn0 =  s->keyBegSecs == 0 ? s->refSmpCnt : s->keySmpCnt;
    unsigned    sn1 =  s->keyBegSecs == 0 ? s->keySmpCnt : s->refSmpCnt;
    const char* dr0 =  s->keyBegSecs == 0 ? refDir       : keyDir;
    const char* dr1 =  s->keyBegSecs == 0 ? keyDir       : refDir;
    const char* ex0 =  s->keyBegSecs == 0 ? refExt       : keyExt;
    const char* ex1 =  s->keyBegSecs == 0 ? keyExt       : refExt;

    const char* ffn0 = _masGenTlFileName( dr0, fn0, ex0 );
    const char* ffn1 = _masGenTlFileName( dr1, fn1, ex1 );

    fcnt = insertFile( fn0, ffn0, fl0, sn0, s->srate, fileArray, fcnt);
    fcnt = insertFile( fn1, ffn1, fl1, sn1, s->srate, fileArray, fcnt);
  }

  // locate the reference file in each sync recd
  for(i=0; i<scp->syncArrayCnt; ++i)
  {
    const syncRecd_t* s   = scp->syncArray + i;
    unsigned          mfi = findFile( s->refFn, kMidiFl,  fileArray, fcnt );
    unsigned          afi = findFile( s->keyFn, kAudioFl, fileArray, fcnt );

    assert( mfi != -1 && afi != -1 );

    fileRecd_t*       mfp = fileArray + mfi;
    fileRecd_t*       afp = fileArray + afi;

    if( s->keyBegSecs == 0 )
    {
      // lock audio to midi
      afp->refIdx    = mfi;
      afp->refPtr    = mfp;
      afp->refSmpIdx = floor( s->refWndBegSecs * s->srate );
      afp->keySmpIdx = s->keySyncIdx; 
    }
    else
    {
      // lock midi to audio
      mfp->refIdx    = afi;
      mfp->refPtr    = afp;  
      mfp->refSmpIdx = s->keySyncIdx;
      mfp->keySmpIdx = floor( s->refWndBegSecs * s->srate );        
    }
  }

  // Calculate the absolute sample indexes and set groupId's.
  // Note that this process depends on reference files being processed before their dependents
  for(i=0; i<fcnt; ++i)
  {
    fileRecd_t* f = fileArray + i;

    // if this is a master file
    if( f->refPtr == NULL )
    {
      f->groupId      = gcnt++;// form a new group 
      f->absSmpIdx    = 0;     // absSmpIdx is meaningless for master files becuase they do not have a sync point
      f->absBegSmpIdx = 0;     // the master file location is always 0
    }
    else // this is a slave file
    {
      f->absSmpIdx    = calcAbsSmpIdx(f);            // calc the absolute time of the sync location
      //f->absBegSmpIdx = f->absSmpIdx - f->keySmpIdx; // calc the absolute begin time of the file 
      f->absBegSmpIdx = f->refSmpIdx - f->keySmpIdx;
      f->groupId      = f->refPtr->groupId;          // set the group id
    }    
  }

  // At this point the absBegSmpIdx of the master file in each group is set to 0
  // and the absBegSmpIdx of slave files is then set relative to 0. This means that
  // some slave files may have negative offsets if they start prior to the master.
  //
  // Set the earliest file in the group to have an absBegSmpIdx == 0 and shift all
  // other files relative to this.  After this process all absBegSmpIdx values will
  // be positive.
  // 
  if(0)
  {
    for(i=0; i<gcnt; ++i)
    {
      int begSmpIdx = -1;
      int j;

      // find the file in groupId==i  with the earliest absolute start time
      for(j=0; j<fcnt; ++j)
      {
        fileRecd_t* f = fileArray + j;
        if( f->groupId==i && (begSmpIdx == -1 || f->absBegSmpIdx < begSmpIdx) )
          begSmpIdx = f->absBegSmpIdx;

      }

      // subtract the earliest absolute start time from all files in groupId==i
      for(j=0; j<fcnt; ++j)
      {
        fileRecd_t* f = fileArray + j;
        if( f->groupId == i )
          f->absBegSmpIdx -= begSmpIdx;
      }
    }
  }

  // fill in the text label assoc'd with each file
  unsigned   acnt = 0;
  unsigned   mcnt = 0;

  for(i=0; i<fcnt; ++i)
  {
    fileRecd_t* f = fileArray + i;

    if( cmIsFlag(f->flags,kAudioFl) )
      snprintf(f->label,kLabelCharCnt,"af-%i",acnt++);
    else
    {
      if( cmIsFlag(f->flags,kMidiFl) )
        snprintf(f->label,kLabelCharCnt,"mf-%i",mcnt++);
      else
      { assert(0); }
    }
  }

  if( fcnt > 0 )
  {
    cmReal_t srate             = fileArray[0].srate;
    unsigned smpsBetweenGroups = floor(secsBetweenGroups * srate );
    masProcFileArray(fileArray,fcnt,smpsBetweenGroups,procFlags);

    rc =  masWriteJsonTimeLine(ctx,fileArray[0].srate,fileArray,fcnt,outFn);

    for(i=0; i<fcnt; ++i)
      cmFsFreeFn(fileArray[i].fullFn);
  }

  return rc;
}


masRC_t sync_files( cmCtx_t* ctx, syncCtx_t* scp )
{
  masRC_t rc = kOkMasRC;
  unsigned i;

  // for each syncRecd
  for(i=0; i<scp->syncArrayCnt; ++i)
  {
    syncRecd_t* s = scp->syncArray + i;

    // form the ref (midi) and key (audio) file names
    const cmChar_t* refFn  = cmFsMakeFn(scp->refDir, s->refFn,  NULL, NULL);
    const cmChar_t* keyFn  = cmFsMakeFn(scp->keyDir, s->keyFn, NULL, NULL);
    
    double keyEndSecs = s->keyEndSecs;

    // if the cur key fn is the same as the next key file.   Use the search start 
    // location (keyBegSecs) of the next sync recd as the search end 
    // location for this file.
    if( i < scp->syncArrayCnt-1 && strcmp(s->keyFn, scp->syncArray[i+1].keyFn) == 0 )
    {
      keyEndSecs = scp->syncArray[i+1].keyBegSecs;
      
      if( keyEndSecs < s->keyBegSecs )
      {
        rc = cmErrMsg(&ctx->err,kParamErrMasRC,"The key file search area start times for for multiple sync records referencing the the same key file should increment in time.");        
      }
    }

    masRC_t rc0;
    if((rc0 = slide_match(ctx,refFn,keyFn,s,scp->hopMs,floor(keyEndSecs*1000))) != kOkMasRC)
    {
      cmErrMsg(&ctx->err,rc0,"Slide match failed on Ref:%s Key:%s.",cmStringNullGuard(refFn),cmStringNullGuard(keyFn));
      rc = rc0;
    }

    printf("\nbeg:%f end:%f sync:%i dist:%f ref:%s key:%s \n",s->keyBegSecs,keyEndSecs,s->keySyncIdx,s->syncDist,refFn,keyFn);

    cmFsFreeFn(keyFn);
    cmFsFreeFn(refFn);
  }

  return rc;
}

void masSyncCtxInit(syncCtx_t* scp)
{
  memset(scp,0,sizeof(syncCtx_t));
  scp->jsH = cmJsonNullHandle;
}

masRC_t masSyncCtxFinalize(cmCtx_t* ctx, syncCtx_t* scp)
{
  masRC_t rc = kOkMasRC;
  if( cmJsonFinalize(&scp->jsH) != kOkJsRC )
    rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"syncCtx JSON finalization failed.");

  cmMemFree(scp->syncArray);
  scp->syncArrayCnt = 0;
  return rc;
}

masRC_t masMidiToImpulse( cmCtx_t* ctx, const masPgmArgs_t* p )
{
  assert(p->input!=NULL && p->output!=NULL);

  if( cmFsIsDir(p->input) )
    return fileDriver(ctx, kMidiToAudioSelId, p->input, p->output, p->srate, 0, NULL );

  return midiToAudio(ctx, p->input, p->output, p->srate );  
}

masRC_t masAudioToOnset( cmCtx_t* ctx, const masPgmArgs_t* p )
{
  assert(p->input!=NULL && p->output!=NULL);

  if( cmFsIsDir(p->input) )
    return fileDriver(ctx, kAudioOnsetSelId, p->input, p->output, 0, 0,  &p->onsetCfg );

  return audioToOnset(ctx, p->input, p->output, &p->onsetCfg );
}

masRC_t masConvolve( cmCtx_t* ctx, const masPgmArgs_t* p )
{
  assert(p->input!=NULL && p->output!=NULL);

  if( cmFsIsDir(p->input) )
    return fileDriver(ctx, kConvolveSelId, p->input, p->output, 0, p->wndMs, NULL );

  return convolve(ctx, p->input, p->output, p->wndMs );  
}

masRC_t masSync( cmCtx_t* ctx, const masPgmArgs_t* p )
{
  masRC_t rc = kOkMasRC,rc0;
  syncCtx_t sc;

  assert(p->input!=NULL && p->output!=NULL);

  masSyncCtxInit(&sc);

  if( (rc = parse_sync_cfg_file(ctx, p->input, &sc )) == kOkMasRC )
    if((rc = sync_files(ctx, &sc )) == kOkMasRC )
      rc = write_sync_json(ctx,&sc,p->output);

  rc0 = masSyncCtxFinalize(ctx,&sc);

  return rc!=kOkMasRC ? rc : rc0;
}


masRC_t masGenTimeLine( cmCtx_t* ctx, const masPgmArgs_t* p )
{
  masRC_t rc,rc0;
  syncCtx_t sc;

  if( p->refDir == NULL )
    return cmErrMsg(&ctx->err,kParamErrMasRC,"A directory must be provided to locate the audio and MIDI files. See the program parameter 'ref-dir'.");

  if( p->keyDir == NULL )
    return cmErrMsg(&ctx->err,kParamErrMasRC,"A directory must be provided to locate the audio and MIDI files. See the program parameter 'key-dir'.");

  assert(p->input!=NULL && p->output!=NULL);

  masSyncCtxInit(&sc);

  if((rc = read_sync_json(ctx,&sc,p->input)) != kOkMasRC )
    goto errLabel;

  // TODO: Add these as program options, also add a --dry-run option 
  // 
  unsigned procFlags = 0; //kZeroBaseTimeMasFl | kSequenceGroupsMasFl | kMakeOneGroupMasFl;
  double   secsBetweenGroups = 60.0;

  if((rc = masCreateTimeLine(ctx, &sc, p->output, p->refDir, p->keyDir, p->refExt, p->keyExt, secsBetweenGroups, procFlags)) != kOkMasRC )
    goto errLabel;

 errLabel:
  rc0  = masSyncCtxFinalize(ctx,&sc);

  return rc!=kOkMasRC ? rc : rc0;
}

// Given a time line file and a marker file, insert the markers in the time line and
// then write the time line to an output file. The marker file must have the following format:
//{
// markerArray : [
//	{ sect:1  beg:630.0       end:680.0       label:"Sec 3 m10"},
//	{ sect:3  beg:505.1       end:512.15      label:"Sec 4 m12"},
//	{ sect:4  beg:143.724490  end:158.624322  label:"Sec 6, 6a  m14-16, #2 (A) slower tempo"},
// ]
//  }
//
// NOTES: 
//  1) beg/end are in seconds, 
//  2) 'sect' refers to the audio file number (e.g. "Piano_01.wav,Piano_03.wav,Piano_04.wav")
//
masRC_t masLoadMarkers( cmCtx_t* ctx, const masPgmArgs_t* p )
{
  assert(p->input!=NULL);
  assert(p->markFn!=NULL);
  assert(p->output!=NULL);

  masRC_t         rc       = kOkMasRC;
  const cmChar_t* tlFn     = p->input;
  const cmChar_t* mkFn     = p->markFn;
  const cmChar_t* outFn    = p->output;
  const cmChar_t* afFmtStr = "/home/kevin/media/audio/20110723-Kriesberg/Audio Files/Piano 3_%02.2i.wav";
  cmTlH_t         tlH      = cmTimeLineNullHandle;
  cmJsonH_t       jsH      = cmJsonNullHandle;
  cmJsonNode_t*   anp      = NULL;

  // create the time line
  if( cmTimeLineInitializeFromFile(ctx, &tlH, NULL, NULL, tlFn, p->prefixPath ) != kOkTlRC )
    return cmErrMsg(&ctx->err,kTimeLineFailMasRC,"Time line created failed on '%s'.", cmStringNullGuard(tlFn));

  // open the marker file
  if( cmJsonInitializeFromFile(&jsH, mkFn, ctx ) != kOkJsRC )
  {
    rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"Marker file open failed on '%s'.",cmStringNullGuard(mkFn));
    goto errLabel;
  }

  // locate the marker array in the marker file
  if((anp = cmJsonFindValue(jsH,"markerArray",NULL,kArrayTId)) == NULL )
  {
    rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"The marker file is missing a 'markerArray' node in '%s'.",cmStringNullGuard(mkFn));
    goto errLabel;
  }

  unsigned i;  
  unsigned markerCnt = cmJsonChildCount(anp);
  for(i=0; i<markerCnt; ++i)
  {
    int         sectId;
    double      begSecs;
    double      endSecs;
    char*       markText = NULL;
    const char* errLabel = NULL;
    cmJsRC_t    jsRC;

    // read the ith marker record
    if((jsRC = cmJsonMemberValues(cmJsonArrayElementC(anp,i), &errLabel, 
      "sect",  kIntTId,    &sectId,
      "beg",   kRealTId,   &begSecs,
      "end",   kRealTId,   &endSecs,
      "label", kStringTId, &markText,
      NULL)) != kOkJsRC )
    {
      if( errLabel != NULL )
        rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"The field '%s' was missing on the marker record at index %i.",errLabel,i);
      else
        rc = cmErrMsg(&ctx->err,kJsonFailMasRC,"An error occurred while reading the marker record at index %i.",i);
      goto errLabel;
    }

    cmChar_t*        afFn = cmTsPrintfS(afFmtStr,sectId);
    cmTlAudioFile_t* tlop;

    // find the audio file this marker refers to in the time line
    if(( tlop = cmTimeLineFindAudioFile(tlH,afFn)) == NULL )
      cmErrWarnMsg(&ctx->err,kParamErrMasRC,"The audio file '%s' associated with the marker record at index %i could not be found in the time line.",cmStringNullGuard(afFn),i);
    else
    {
      // convert the marker seconds to samples
      unsigned begSmpIdx = floor(cmTimeLineSampleRate(tlH) * begSecs);
      unsigned durSmpCnt = floor(cmTimeLineSampleRate(tlH) * endSecs) - begSmpIdx;

      // insert the marker into the time line
      if( cmTimeLineInsert(tlH,cmTsPrintfS("Mark %i",i),kMarkerTlId,markText,begSmpIdx,durSmpCnt,tlop->obj.name, tlop->obj.seqId) != kOkTlRC )
      {
        rc = cmErrMsg(&ctx->err,kTimeLineFailMasRC,"Marker record insertion failed for marker at record index %i.",i);
        goto errLabel;
      }
    }
    
  }

  // write the time line as a JSON file
  if( cmTimeLineWrite(tlH,outFn) != kOkTlRC )
  {
    rc = cmErrMsg(&ctx->err,kTimeLineFailMasRC,"Time line write to '%s'. failed.",cmStringNullGuard(outFn));
    goto errLabel;
  }

 errLabel:
  cmJsonFinalize(&jsH);
  cmTimeLineFinalize(&tlH);
  return rc;
}

masRC_t masTestStub( cmCtx_t* ctx, const masPgmArgs_t* p )
{
  //return masSync(ctx,p);
  masRC_t rc = kOkMasRC;

  const char* scFn = "/home/kevin/src/mas/src/data/mod0.txt";
  const char* tlFn = "/home/kevin/src/mas/src/data/tl3.js";
  const char* mdDir= "/home/kevin/media/audio/20110723-Kriesberg/midi";

  if(0)
  {
    cmMidiByte_t x[] = { 37, 65, 87 };
    midiStringSearch(ctx, mdDir, x, sizeof(x)/sizeof(x[0]) );
    return rc;
  }

  if(1)
  {
    const cmChar_t* aFn = "/Users/kevin/temp/mas/sine_96k_24bits.aif";
    double srate = 96000;
    unsigned bits = 24;
    double hz = 1;
    double gain = 1;
    double secs = 1;
    cmAudioFileSine( ctx, aFn, srate, bits, hz, gain, secs );
    return rc;
  }

  cmTimeLinePrintFn(ctx, tlFn, p->prefixPath, &ctx->rpt );
  return rc;

  //cmScoreSyncTimeLineTest(ctx, tlFn, scFn );
  //return rc;

  cmScoreTest(ctx,scFn);
  return rc;

  cmTimeLineTest(ctx,tlFn,p->prefixPath);
  return rc;

  
  //const char* inFn    = "/home/kevin/temp/mas/out0.bin";
  //const char* faFn   = "/home/kevin/temp/mas/file0.bin";
  //const char* outFn = "/home/kevin/src/mas/src/data/file0.js";
  //const char* mdir = "/home/kevin/media/audio/20110723-Kriesberg/midi";
  //const char* adir = "/home/kevin/media/audio/20110723-Kriesberg/Audio Files";
  //createFileArray(ctx, inFn, outFn );
  //printFileArray( ctx, faFn, outFn, adir, mdir);

  return rc;
}



int main( int argc, char* argv[] )
{

  // initialize the heap check library
  bool        memDebugFl      = cmDEBUG_FL;
  unsigned    memPadByteCnt   = memDebugFl ? 8 : 0;
  unsigned    memAlignByteCnt = 16;
  unsigned    memFlags        = memDebugFl ? kTrackMmFl | kDeferFreeMmFl | kFillUninitMmFl : 0; 
  masRC_t     rc              = kOkMasRC;
  cmPgmOptH_t poH             = cmPgmOptNullHandle;
  cmCtx_t     ctx;
  masPgmArgs_t args;

  enum 
  {
    kInputFileSelId = kBasePoId,
    kOutputFileSelId,
    kExecSelId,
    kWndMsSelId,
    kHopFactSelId,
    kAudioChIdxSelId,
    kWndFrmCntSelId,
    kPreWndMultSelId,
    kThresholdSelId,
    kMaxFreqHzSelId,
    kFiltCoeffSelId,
    kPreDlyMsSelId,
    kMedFltWndMsSelId,
    kFilterSelId,
    kSmthFiltSelId,
    kMedianFiltSelId,
    kSrateSelId,
    kRefDirSelId,
    kKeyDirSelId,
    kRefExtSelId,
    kKeyExtSelId,
    kMarkFnSelId,
    kPrefixPathSelId,
  };

  const cmChar_t helpStr0[] =
  {
    //        1         2         3         4         5         6         7         8         
  "Usage: mas -{m|a|c} -i 'input' -o 'output' <options>\n\n"
  };

  const cmChar_t helpStr1[] =
  {
    //        1         2         3         4         5         6         7         8         
    "If --input option specifies a directory then all files in the directory are\n"
"taken as input files.  In this case the names of the output files are generated\n"
"automatically and the --ouptut option must specify a directory to receive all\n"
"the output files.\n\nIf the --input option specifies a file then the --output\n"
" option should specifiy the complete name of the output file.\n"
  };

  memset(&args,0,sizeof(args));
  
  cmCtxSetup(&ctx,"Project",print,print,NULL,memPadByteCnt,memAlignByteCnt,memFlags);
  cmMdInitialize( memPadByteCnt, memAlignByteCnt, memFlags, &ctx.rpt );
  cmFsInitialize( &ctx, "mas" );
  cmTsInitialize( &ctx );
  cmPgmOptInitialize(&ctx,&poH,helpStr0,helpStr1);
  

  //                  poH   numId          charId wordId              flags       enumId           default      return ptr               cnt  help string
  cmPgmOptInstallStr( poH, kInputFileSelId,   'i', "input",           kReqPoFl,                    NULL,        &args.input,                 1, "Input file or directory." );
  cmPgmOptInstallStr( poH, kOutputFileSelId,  'o', "output",          kReqPoFl,                    NULL,        &args.output,                1, "Output file or directory." );
  cmPgmOptInstallEnum(poH, kExecSelId,        'm', "midi_to_impulse", kReqPoFl,  kMidiToAudioSelId,cmInvalidId, &args.selId,                 1, "Create an audio impulse file from a MIDI file.","Command Code" );
  cmPgmOptInstallEnum(poH, kExecSelId,        'a', "onsets",          kReqPoFl,  kAudioOnsetSelId, cmInvalidId, &args.selId,                 1, "Create an audio impulse file from the audio event onset detector.",NULL );
  cmPgmOptInstallEnum(poH, kExecSelId,        'c', "convolve",        kReqPoFl,  kConvolveSelId,   cmInvalidId, &args.selId,                 1, "Convolve a Hann window with an audio file.",NULL );
  cmPgmOptInstallEnum(poH, kExecSelId,        'y', "sync",            kReqPoFl,  kSyncSelId,       cmInvalidId, &args.selId,                 1, "Run a synchronization process based on a JSON sync control file and generate a sync. output JSON file..",NULL);
  cmPgmOptInstallEnum(poH, kExecSelId,        'g', "gen_time_line",   kReqPoFl,  kGenTimeLineSelId,cmInvalidId, &args.selId,                 1, "Generate a time-line JSON file from a sync. output JSON file.",NULL);
  cmPgmOptInstallEnum(poH, kExecSelId,        'k', "markers",         kReqPoFl,  kLoadMarkersSelId,cmInvalidId, &args.selId,                 1, "Read markers into the time line.",NULL);
  cmPgmOptInstallEnum(poH, kExecSelId,        'T', "test",            kReqPoFl,  kTestStubSelId,   cmInvalidId, &args.selId,                 1, "Run the test stub.",NULL ),
  cmPgmOptInstallDbl( poH, kWndMsSelId,       'w', "wnd_ms",          0,                           42.0,        &args.wndMs,                 1, "Analysis window look in milliseconds."     );
  cmPgmOptInstallUInt(poH, kHopFactSelId,     'f', "hop_factor",      0,                           4,           &args.onsetCfg.hopFact,      1, "Sliding window hop factor 1=1:1 2=1:2 4=1:4 ...");
  cmPgmOptInstallUInt(poH, kAudioChIdxSelId,  'u', "ch_idx",          0,                           0,           &args.onsetCfg.audioChIdx,   1, "Audio channel index.");
  cmPgmOptInstallUInt(poH, kWndFrmCntSelId,   'r', "wnd_frm_cnt",     0,                           3,           &args.onsetCfg.wndFrmCnt,    1, "Audio onset window frame count.");  
  cmPgmOptInstallDbl( poH, kPreWndMultSelId,  'x', "wnd_pre_mult",    0,                           3,           &args.onsetCfg.preWndMult,   1, "Audio onset pre-window multiplier.");
  cmPgmOptInstallDbl( poH, kThresholdSelId,   't', "threshold",       0,                           0.6,         &args.onsetCfg.threshold,    1, "Audio onset threshold value.");
  cmPgmOptInstallDbl( poH, kMaxFreqHzSelId,   'z', "max_frq_hz",      0,                           20000,       &args.onsetCfg.maxFrqHz,     1, "Audio onset maximum analysis frequency.");
  cmPgmOptInstallDbl( poH, kFiltCoeffSelId,   'e', "filt_coeff",      0,                           0.7,         &args.onsetCfg.filtCoeff,    1, "Audio onset smoothing filter coefficient.");
  cmPgmOptInstallDbl( poH, kPreDlyMsSelId,    'd', "pre_delay_ms",    0,                           0,           &args.onsetCfg.preDelayMs,   1, "Move each detected audio onset backwards in time by this amount.");
  cmPgmOptInstallDbl( poH, kMedFltWndMsSelId, 'l',"med_flt_wnd_ms",   0,                           50,          &args.onsetCfg.medFiltWndMs, 1, "Length of the onset detection median filter.  Ignored if the median filter is not used.");
  cmPgmOptInstallEnum(poH, kFilterSelId,      'b', "smooth_filter",   0,         kSmthFiltSelId,   cmInvalidId, &args.onsetCfg.filterId,     1, "Apply a smoothing filter to the onset detection function.","Audio onset filter");
  cmPgmOptInstallEnum(poH, kFilterSelId,      'n', "median_filter",   0,         kMedianFiltSelId, cmInvalidId, &args.onsetCfg.filterId,     1, "Apply a median filter to the onset detections function.", NULL );
  cmPgmOptInstallDbl( poH, kSrateSelId,       's', "sample_rate",     0,                           44100,       &args.srate,                 1, "MIDI to impulse output sample rate.");
  cmPgmOptInstallStr( poH, kRefDirSelId,      'R', "ref_dir",         0,                           NULL,        &args.refDir,                1, "Location of the reference files. Only used with 'gen_time_line'.");
  cmPgmOptInstallStr( poH, kKeyDirSelId,      'K', "key_dir",         0,                           NULL,        &args.keyDir,                1, "Location of the key files. Only used with 'gen_time_line'.");
  cmPgmOptInstallStr( poH, kRefExtSelId,      'M', "ref_ext",         0,                           NULL,        &args.refExt,                1, "Reference file extension. Only used with 'gen_time_line'.");
  cmPgmOptInstallStr( poH, kKeyExtSelId,      'A', "key_ext",         0,                           NULL,        &args.keyExt,                1, "Key file extension. Only used with 'gen_time_line'.");
  cmPgmOptInstallStr( poH, kMarkFnSelId,      'E', "mark_fn",         0,                           NULL,        &args.markFn,                1, "Marker file name");
  cmPgmOptInstallStr( poH, kPrefixPathSelId,  'P', "prefix_path",     0,                           NULL,        &args.prefixPath,            1, "Time Line data file prefix path");


  if((rc = cmPgmOptRC(poH,kOkPoRC)) != kOkPoRC )
    goto errLabel;

  if( cmPgmOptParse(poH, argc, argv ) != kOkPoRC )
    goto errLabel;
  
  if( cmPgmOptHandleBuiltInActions(poH,&ctx.rpt) )
  {
    switch( args.selId )
    {
      case kMidiToAudioSelId:
        masMidiToImpulse(&ctx,&args);
        break;

      case kAudioOnsetSelId:
        args.onsetCfg.wndMs = args.wndMs;
        switch( args.onsetCfg.filterId )
        {
          case kSmthFiltSelId:   args.onsetCfg.filterId = kSmoothFiltId; break;
          case kMedianFiltSelId: args.onsetCfg.filterId = kMedianFiltId; break;
          default:
            args.onsetCfg.filterId = 0;
        }
        
        masAudioToOnset(&ctx,&args);
        break;

      case kConvolveSelId:
        masConvolve(&ctx,&args);
        break;

      case kSyncSelId:
        masSync(&ctx,&args);
        break;

      case kGenTimeLineSelId:
        masGenTimeLine(&ctx,&args);
        break;

      case kLoadMarkersSelId:
        masLoadMarkers(&ctx,&args);
        break;

      case kTestStubSelId:
        masTestStub(&ctx,&args);
        break;

      default:
        { assert(0); }
    }
  }

 errLabel:
  cmPgmOptFinalize(&poH);
  cmTsFinalize();
  cmFsFinalize();
  cmMdReport( kIgnoreNormalMmFl );
  cmMdFinalize();
  return rc;

}
/*
Use Cases:
1) Synchronize Audio to MIDI based on onset patterns:

   a) Convert MIDI to audio impulse files:

      mas -m -i <midi_dir | midi_fn >  -o <out_dir> -s <srate>

      Notes:
      1) If <midi_dir> is given then use all files
         in the directory as input otherwise convert a 
         single file.
      2) The files written to <out_dir> are audio files with
         impulses written at the location of note on msg's.
         The amplitude of the the impulse is velocity/127.

   b) Convert the onsets in audio file(s) to audio impulse
      file(s).

      mas -a -i <audio_dir | audio_fn > -o <out_dir>   
          -w <wndMs> -f <hopFactor> -u <chIdx> -r <wnd_frm_cnt> 
          -x <preWndMult> -t <threshold> -z <maxFrqHz> -e <filtCoeff>

      1) If <audio_dir> is given then use all files
         in the directory as input otherwise convert a 
         single file.
      2) The onset detector uses a spectral flux based
         algorithm.
         See cmOnset.h/.c for an explanation of the 
         onset detection parameters.
         

   c) Convolve impulse files created in a) and b) with a 
      Hann window to widen the impulse width.

      mas -c -i <audio_dir | audio_fn > -o <out_dir> -w <wndMs>

      1) If <audio_dir> is given then use all files
         in the directory as input otherwise convert a 
         single file.
      2) <wndMs> gives the width of the Hann window.
      
   d) Synchronize MIDI and Audio based convolved impulse
      files based on their onset patterns.

      mas -y -i <sync_cfg_fn.js> -o <sync_out_fn.js>

      1) The <sync_cfg_fn.js> file has the following format:
        {
          ref_dir : "/home/kevin/temp/mas/midi_conv"    // location of ref files
          key_dir : "/home/kevin/temp/mas/onset_conv"   // location of key files
          hop_ms  : 25                                  // sliding window increment

          sync_array :
          [
            //   ref_fn  wnd_beg_secs wnd_dur_secs  key_fn        key_beg_secs, key_end_secs
            [    "1.aif",    678,         113,    "Piano 3_01.aif",  239.0,     417.0], 
            [    "3.aif",    524,          61,    "Piano 3_06.aif",  556.0,     619.0],
          ]
        }

         Notes:
         a. The 'window' is the section of the reference file which is compared
            to the key file search area <key_beg_secs> to <key_end_secs> by sliding it 
            in increments of 'hop_ms' samples.

         b. Set 'key_end_secs' to 0 to search to the end of the file.

         c. When one key file matches to multiple reference files the
            key files sync recd should be listed consecutively.  This way
            the earlier searches can stop when they reach the beginning 
            of the next sync records search region.  See sync_files().

            Note that by setting  <key_beg_secs> to a non-zero value
            as occurs in the multi-key-file case has a subtle effect of
            changing the master-slave relationship between the reference
            an key file.  

            In general the reference file is the master and the key file
            is the slave.  When a non-zero <key_beg_secs> is given however
            this relationship reverses.  See masCreateTimeLine() for 
            how this is used to assign file group id's during the
            time line creation.

      3) The <sync_out_fn.js> has the following form.

         {
           "sync" : 
           {
             "refDir" : "/home/kevin/temp/mas/midi_conv"     
             "keyDir" : "/home/kevin/temp/mas/onset_conv"     
             "hopMs" : 25.000000     

             "array" : 
             [
              
               //
               // sync results for "1.aif" to "Piano 3_01.aif"
               //

               {
                 // The following block of fields were copied from  <sync_cfg_fn.js>. 
                 "refFn"         : "1.aif"         
                 "refWndBegSecs" : 678.000000         
                 "refWndSecs"    : 113.000000         
                 "keyFn"         : "Piano 3_01.aif"         
                 "keyBegSecs"    : 239.000000         
                 "keyEndSecs"    : 417.000000 
        
                 // Sync. location of the 'window' in the key file.
                 // Sample index into the key file which matches to the first sample 
                 // in the reference window.
                 "keySyncIdx" : 25768800     // Offset into the key file of the best match.
    
                 "syncDist"  : 4184.826108   // Match distance score for the sync location.          
                 "refSmpCnt" : 200112000     // Count of samples in the reference file.       
                 "keySmpCnt" : 161884800     // Count of samples in the key file.        
                 "srate"     : 96000.000000  // Sample rate of the reference and key file.
               },
             ]    
           }  
         }

2) Create a time line from the results of a synchronization.  A time line is a data structure
   (See cmTimeLine.h/.c) which maintains a time based ordering of Audio files, MIDI files,
   and arbitrary markers.

  mas -g -i <sync_out_fn.js>  -o <time_line_out_fn.js> -R <ref_dir> -K <key_dir> -M <ref_ext> -A <key_ext>

  <sync_out_fn.js> The output file produced as a result of a previous MIDI <-> Audio synchronization.
  
  <ref_dir>        Location of the reference files (MIDI) used for the synchronization. 
  <ref_ext>        File extension used by the reference files.
  <key_dir>        Locate of the key files (Audio) used for the synchronization.
  <key_ext>        File extension used by the key files.

  1) The time line 'trackId' assigned to each time line object is based on the files
     'groupId'. A common group id is given to sets of files which are
     locked in time relative to one another.  For example
     if file B and C are synced to master file A and
     file D is synced to file E which is synced to master
     file F.  Then files A,B,C will be given one group
     id and files D,E and F will be given another group id.
     (See masCreateTimeLine()).

  2) The time line object 'offset' values gives the offset in samples where the object
     begins relative to other objects in the group.  Note that the master object in the
     group may not begin at offset 0 if there are slave objects which start before it.


     
 */

/* MIDI File Durations (rounded to next minute)


1    35   678 113  01    0
2    30    53 114  03    0
          655 116  04    0
         1216 102  05    0 
3    19   524  61  06    0
          958  40  07    0
4    15   206  54  08    0
          797  40  09    0
5    40   491 104  11    0
         1712 109  12    0
         2291  84  13    0
6    44   786 105  13  299
         1723 112  14    0
7     3    99  41  15    0
8    38   521  96  17    0
         1703  71  18    0
9    31   425 104  19    0
10    2    16  19  21    0
12   10   140  87  21  222
13   14   377  58  21  942
15   18    86  71  21 1975
          593  79  22    0 
16-2 16   211  75  23    0
17-1  8   129  38  24    0
17-2 16   381  54  26    0
18   22   181  98  27    0
19   22   134  57  28    0
20    7    68  44  29    0
*/
