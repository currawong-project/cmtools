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

#include "cmXScore.h"
#include "cmMidiScoreFollow.h"
#include "cmScoreProc.h"


#include "cmSymTbl.h"
#include "cmTime.h"
#include "cmMidi.h"
#include "cmScore.h"

#include "cmMidiFile.h"

#include "cmFloatTypes.h"
#include "cmAudioFile.h"
#include "cmTimeLine.h"

enum
{
 kOkCtRC = cmOkRC,
 kNoActionIdSelectedCtRC,
 kMissingRequiredFileNameCtRC,
 kScoreGenFailedCtRC,
 kScoreFollowFailedCtRC,
 kMidiFileRptFailedCtRC,
 kTimeLineRptFailedCtRC,
 kAudioFileRptFailedCtRC
};


const cmChar_t poEndHelpStr[] = "";
const cmChar_t poBegHelpStr[] =
  "xscore_proc Music XML to electronic score generator\n"
  "\n"
  "USAGE:\n"
  "\n"
  "Parse an XML score file and decoration file to produce a score file in CSV format.\n"
  "\n"
  "cmtool --score_gen -x <xml_file> -d <dec_fn> {-c <csvScoreOutFn} {-m <midiOutFn>} {-s <svgOutFn>} {-r report} {-b begMeasNumb} {t begTempoBPM}\n"
  "\n"
  "Notes:\n"
  "1.  If <dec_fn> does not exist then a decoration template file will be generated based on the MusicXML file. \n"
  "2.  Along with the CSV score file MIDI and HTML/SVG files will also be produced based on the contents of the MusicXML and decoration file.\n"
  "3. See README.md for a detailed description of the how to edit the decoration file.\n"
  "\n"
  "\n"
  "Use the score follower to generate a timeline configuration file.\n"
  "\n"
  "cmtool --timeline_gen -c <csvScoreFn> -i <midiInFn> -r <matchRptFn> -s <matchSvgFn> {-m <midiOutFn>} {-t timelineOutFn} \n"
  "\n"
  "Measure some perforamance attributes:\n"
  "\n"
  "cmtool --meas_gen -g <pgmRsrcFn> -r <measRptFn>\n"
  "\n"
  "Generate a score file report\n"
  "\n"
  "cmtool --score_report -c <csvScoreFn> -r <scoreRptFn>\n"
  "\n"
  "Generate a MIDI file report and optional SVG piano roll image\n"
  "\n"
  "cmtool --midi_report -i <midiInFn> -r <midiRptFn> {-s <svgOutFn>}\n"
  "\n"
  "Generate a timeline report\n"
  "\n"
  "cmtool --timeline_report -t <timelineFn> -r <timelineRptFn>\n"
  "\n"
  "Generate an audio file report\n"
  "\n"
  "cmtool --audiofile_report -a <audioFn> -r <rptFn>\n"
  "\n";


void print( void* arg, const char* text )
{
  printf("%s",text);
}

bool verify_file_exists( cmCtx_t* ctx, const cmChar_t* fn, const cmChar_t* msg )
{
  if( fn == NULL || cmFsIsFile(fn)==false )
    return cmErrMsg(&ctx->err,kMissingRequiredFileNameCtRC,"The required file <%s> does not exist.",msg);

  return kOkCtRC;
}

bool verify_non_null_filename( cmCtx_t* ctx, const cmChar_t* fn, const cmChar_t* msg )
{
  if( fn == NULL )
    return cmErrMsg(&ctx->err,kMissingRequiredFileNameCtRC,"The required file name <%s> is blank.",msg);

  return kOkCtRC;
}

cmRC_t score_gen( cmCtx_t* ctx, const cmChar_t* xmlFn, const cmChar_t* decFn, const cmChar_t* csvOutFn, const cmChar_t* midiOutFn, const cmChar_t* svgOutFn, unsigned reportFl, int begMeasNumb, int begTempoBPM, bool svgStandAloneFl, bool svgPanZoomFl, bool damperRptFl )
{
  cmRC_t rc;
  if((rc = verify_file_exists(ctx,xmlFn,"XML file")) != kOkCtRC )
    return rc;
  
  if( cmXScoreTest( ctx, xmlFn, decFn, csvOutFn, midiOutFn, svgOutFn, reportFl, begMeasNumb, begTempoBPM, svgStandAloneFl, svgPanZoomFl, damperRptFl ) != kOkXsRC )
    return cmErrMsg(&ctx->err,kScoreGenFailedCtRC,"score_gen failed.");
    
  return kOkCtRC;
}

cmRC_t score_follow( cmCtx_t* ctx, const cmChar_t* csvScoreFn, const cmChar_t* midiInFn, const cmChar_t* matchRptOutFn, const cmChar_t* matchSvgOutFn,  const cmChar_t* midiOutFn, const cmChar_t* timelineFn )
{
  cmRC_t rc;
  
  if((rc = verify_file_exists(ctx,csvScoreFn,"Score CSV file")) != kOkCtRC )
    return rc;

  if((rc = verify_file_exists(ctx,midiInFn,"MIDI input file")) != kOkCtRC )
    return rc;

  //if((rc = verify_file_exists(ctx,matchRptOutFn,"Match report file")) != kOkCtRC )
  //  return rc;

  //if((rc = verify_file_exists(ctx,matchSvgOutFn,"Match HTML/SVG file")) != kOkCtRC )
  //  return rc;
  
  if(cmMidiScoreFollowMain(ctx, csvScoreFn, midiInFn, matchRptOutFn, matchSvgOutFn, midiOutFn, timelineFn) != kOkMsfRC )
    return cmErrMsg(&ctx->err,kScoreFollowFailedCtRC,"score_follow failed.");
    
  return kOkCtRC;         
}

cmRC_t meas_gen( cmCtx_t* ctx, const cmChar_t* pgmRsrcFn, const cmChar_t* outFn )
{
  cmRC_t rc;
  
  if((rc = verify_file_exists(ctx,pgmRsrcFn,"Program resource file")) != kOkCtRC )
    return rc;

  if((rc = verify_non_null_filename( ctx,outFn,"Measurements output file.")) != kOkCtRC )
    return rc;
  
  return cmScoreProc(ctx, "meas", pgmRsrcFn, outFn );
}

cmRC_t score_report( cmCtx_t* ctx, const cmChar_t* csvScoreFn, const cmChar_t* rptFn )
{
  cmRC_t rc;
  
  if((rc = verify_file_exists(ctx,csvScoreFn,"Score CSV file")) != kOkCtRC )
    return rc;

  cmScoreReport(ctx,csvScoreFn,rptFn);
  
  return rc;
}


cmRC_t midi_file_report( cmCtx_t* ctx, const cmChar_t* midiFn, const cmChar_t* rptFn, const cmChar_t* svgFn, bool standAloneFl, bool panZoomFl )
{
  cmRC_t rc ;

  if((rc = verify_file_exists(ctx,midiFn,"MIDI file")) != kOkCtRC )
    return rc;
  
  if((rc = cmMidiFileReport(ctx, midiFn, rptFn )) != kOkMfRC )
    return cmErrMsg(&ctx->err,kMidiFileRptFailedCtRC,"MIDI file report generation failed.");  

  if( svgFn != NULL )
    if((rc = cmMidiFileGenSvgFile(ctx, midiFn, svgFn, "midi_file_svg.css", standAloneFl, panZoomFl )) != kOkMfRC )
      return cmErrMsg(&ctx->err,kMidiFileRptFailedCtRC,"MIDI file SVG output generation failed.");
  
  return kOkCtRC;
}

cmRC_t timeline_report( cmCtx_t* ctx, const cmChar_t* timelineFn, const cmChar_t* tlPrefixPath, const cmChar_t* rptFn )
{
  cmRC_t rc ;

  if((rc = verify_file_exists(ctx,timelineFn,"Timeline file")) != kOkCtRC )
    return rc;

  if((rc = cmTimeLineReport( ctx, timelineFn, tlPrefixPath, rptFn  )) != kOkTlRC )
    return cmErrMsg(&ctx->err,kTimeLineRptFailedCtRC,"The timeline file report failed.");

  return rc;
}

cmRC_t audio_file_report( cmCtx_t* ctx, const cmChar_t* audioFn, const cmChar_t* rptFn )
{
  cmRC_t rc;

  if((rc = verify_file_exists(ctx,audioFn,"Audio file")) != kOkCtRC )
    return rc;

  if((rc = cmAudioFileReportInfo( ctx, audioFn, rptFn  )) != kOkTlRC )
    return cmErrMsg(&ctx->err,kAudioFileRptFailedCtRC,"The audio file report failed.");

  return rc;
}


int main( int argc, char* argv[] )
{
  cmRC_t rc = cmOkRC;
  enum
  {
   kInvalidPoId = kBasePoId,
   kActionPoId,
   kXmlFileNamePoId,
   kDecorateFileNamePoId,
   kCsvOutFileNamePoId,
   kPgmRsrcFileNamePoId,
   kMidiOutFileNamePoId,
   kMidiInFileNamePoId,
   kSvgOutFileNamePoId,
   kStatusOutFileNamePoId,
   kTimelineFileNamePoId,
   kTimelinePrefixPoId,
   kAudioFileNamePoId,
   kReportFlagPoId,
   kSvgStandAloneFlPoId,
   kSvgPanZoomFlPoId,
   kBegMeasPoId,
   kBegBpmPoId,
   kDamperRptPoId,
   kBegMidiUidPoId,
   kEndMidiUidPoId
  };

  enum {
        kNoSelId,
        kScoreGenSelId,
        kScoreFollowSelId,
        kMeasGenSelId,
        kScoreReportSelId,
        kMidiReportSelId,
        kTimelineReportSelId,
        kAudioReportSelId
  };
    
  
  // initialize the heap check library
  bool            memDebugFl      = 0; //cmDEBUG_FL;
  unsigned        memGuardByteCnt = memDebugFl ? 8 : 0;
  unsigned        memAlignByteCnt = 16;
  unsigned        memFlags        = memDebugFl ? kTrackMmFl | kDeferFreeMmFl | kFillUninitMmFl : 0;  
  cmPgmOptH_t     poH             = cmPgmOptNullHandle;
  const cmChar_t* appTitle        = "cmtools";
  cmCtx_t         ctx;
  const cmChar_t* xmlFn           = NULL;
  const cmChar_t* decFn           = NULL;
  const cmChar_t* pgmRsrcFn       = NULL;
  const cmChar_t* csvScoreFn      = NULL;
  const cmChar_t* midiOutFn       = NULL;
  const cmChar_t* midiInFn        = NULL;
  const cmChar_t* audioFn         = NULL;
  const cmChar_t* svgOutFn        = NULL;
  const cmChar_t* timelineFn      = NULL;
  const cmChar_t* timelinePrefix  = NULL;
  const cmChar_t* rptFn           = NULL;
  unsigned        reportFl        = 0;
  unsigned        svgStandAloneFl = 1;
  unsigned        svgPanZoomFl    = 1;
  int             begMeasNumb     = 0;
  int             begTempoBPM     = 60;
  unsigned        damperRptFl     = 0;
  unsigned        begMidiUId      = cmInvalidId;
  unsigned        endMidiUId      = cmInvalidId;
  unsigned        actionSelId     = kNoSelId;
    
  cmCtxSetup(&ctx,appTitle,print,print,NULL,memGuardByteCnt,memAlignByteCnt,memFlags);

  cmMdInitialize( memGuardByteCnt, memAlignByteCnt, memFlags, &ctx.rpt );

  cmFsInitialize( &ctx, appTitle);

  cmTsInitialize(&ctx );

  cmPgmOptInitialize(&ctx, &poH, poBegHelpStr, poEndHelpStr );

  cmPgmOptInstallEnum( poH, kActionPoId, 'S', "score_gen",    0, kScoreGenSelId,    kNoSelId,  &actionSelId, 1,
    "Run the score generation tool.","Action selector");

  cmPgmOptInstallEnum( poH, kActionPoId, 'F', "score_follow", 0, kScoreFollowSelId, kNoSelId,  &actionSelId, 1,
    "Run the time line marker generation tool.",NULL);

  cmPgmOptInstallEnum( poH, kActionPoId, 'M', "meas_gen",     0, kMeasGenSelId,     kNoSelId,  &actionSelId, 1,
    "Generate perfomance measurements.",NULL);

  cmPgmOptInstallEnum( poH, kActionPoId, 'R', "score_report", 0, kScoreReportSelId, kNoSelId,  &actionSelId, 1,
    "Generate a score file report.",NULL);

  cmPgmOptInstallEnum( poH, kActionPoId, 'I', "midi_report", 0, kMidiReportSelId, kNoSelId,  &actionSelId, 1,
    "Generate a MIDI file report and optional SVG piano roll output.",NULL);

  cmPgmOptInstallEnum( poH, kActionPoId, 'E', "timeline_report", 0, kTimelineReportSelId, kNoSelId,  &actionSelId, 1,
    "Generate a timeline report.",NULL);

  cmPgmOptInstallEnum( poH, kActionPoId, 'A', "audio_report",    0, kAudioReportSelId, kNoSelId,  &actionSelId, 1,
    "Generate an audio file report.",NULL);

  
  cmPgmOptInstallStr( poH, kXmlFileNamePoId,      'x', "muisic_xml_fn",0,    NULL,         &xmlFn,        1, 
    "Name of the input MusicXML file.");

  cmPgmOptInstallStr( poH, kDecorateFileNamePoId, 'd', "dec_fn",       0,    NULL,         &decFn,        1, 
    "Name of a score decoration file.");

  cmPgmOptInstallStr( poH, kCsvOutFileNamePoId,   'c', "score_csv_fn",0,    NULL,         &csvScoreFn,    1, 
    "Name of a CSV score file.");

  cmPgmOptInstallStr( poH, kPgmRsrcFileNamePoId,  'g', "pgm_rsrc_fn", 0,     NULL,         &pgmRsrcFn,     1, 
    "Name of program resource file.");
  
  cmPgmOptInstallStr( poH, kMidiOutFileNamePoId,  'm', "midi_out_fn",  0,    NULL,         &midiOutFn,     1, 
    "Name of a MIDI file to generate as output.");
  
  cmPgmOptInstallStr( poH, kMidiInFileNamePoId,   'i', "midi_in_fn",   0,    NULL,         &midiInFn,      1, 
    "Name of a MIDI file to generate as output.");
  
  cmPgmOptInstallStr( poH, kSvgOutFileNamePoId,   's', "svg_fn",       0,    NULL,         &svgOutFn,      1, 
    "Name of a HTML/SVG file to generate as output.");

  cmPgmOptInstallStr( poH, kTimelineFileNamePoId, 't', "timeline_fn",  0,    NULL,         &timelineFn,    1,
    "Name of a timeline to generate as output.");
  
  cmPgmOptInstallStr( poH, kTimelinePrefixPoId,   'l', "tl_prefix",    0,    NULL,         &timelinePrefix,1,
    "Timeline data path prefix.");

  cmPgmOptInstallStr( poH, kAudioFileNamePoId,    'a', "audio_fn",    0,    NULL,          &audioFn,       1,
    "Audio file name.");
  
  cmPgmOptInstallStr( poH, kStatusOutFileNamePoId,'r', "report_fn",    0,    NULL,         &rptFn,         1, 
    "Name of a status file to generate as output.");
  
  cmPgmOptInstallFlag( poH, kReportFlagPoId,      'f', "debug_fl",     0,       1,         &reportFl,      1,
    "Print a report of the score following processing." );

  cmPgmOptInstallInt( poH, kBegMeasPoId,          'b', "beg_meas",     0,       1,         &begMeasNumb,   1,
    "The first measure the to be written to the output CSV, MIDI and SVG files." );

  cmPgmOptInstallInt( poH, kBegBpmPoId,           'e', "beg_bpm",      0,       0,          &begTempoBPM,  1,
    "Set to 0 to use the tempo from the score otherwise set to use the tempo at begMeasNumb." );

  cmPgmOptInstallFlag( poH, kDamperRptPoId,        'u', "damper",       0,      1,          &damperRptFl,  1,
    "Print the pedal events during 'score_gen' processing.");

  cmPgmOptInstallFlag( poH, kSvgStandAloneFlPoId,  'n', "svg_stand_alone_fl",0, 1,          &svgStandAloneFl, 1,
    "Write the SVG file as a stand alone HTML file. Enabled by default." );

  cmPgmOptInstallFlag( poH, kSvgPanZoomFlPoId,     'z', "svg_pan_zoom_fl", 0,   1,          &svgPanZoomFl, 1,
    "Include the pan-zoom control. Enabled by default." );

  cmPgmOptInstallUInt( poH, kBegMidiUidPoId,        'w', "beg_midi_uid",    0,   1,          &begMidiUId,   1,
    "Begin MIDI msg. uuid." );

  cmPgmOptInstallUInt( poH, kEndMidiUidPoId,        'y', "end_midi_uid",    0,   1,          &endMidiUId,   1,
    "End MIDI msg. uuid." );
  
  // parse the command line arguments
  if( cmPgmOptParse(poH, argc, argv ) == kOkPoRC )
  {
    // handle the built-in arg's (e.g. -v,-p,-h)
    // (returns false if only built-in options were selected)
    if( cmPgmOptHandleBuiltInActions(poH, &ctx.rpt ) == false )
      goto errLabel;

    switch( actionSelId )
    {
      case kScoreGenSelId:
        rc = score_gen( &ctx, xmlFn, decFn, csvScoreFn, midiOutFn, svgOutFn, reportFl, begMeasNumb, begTempoBPM, svgStandAloneFl, svgPanZoomFl, damperRptFl );
        break;

      case kScoreFollowSelId:
        rc = score_follow( &ctx, csvScoreFn, midiInFn, rptFn, svgOutFn,  midiOutFn, timelineFn );
        break;

      case kMeasGenSelId:
        rc = meas_gen(&ctx, pgmRsrcFn, rptFn);
        break;

      case kScoreReportSelId:
        rc = score_report(&ctx, csvScoreFn, rptFn );
        break;

      case kMidiReportSelId:
        rc = midi_file_report(&ctx, midiInFn, rptFn, svgOutFn, svgStandAloneFl, svgPanZoomFl );
        break;

      case kTimelineReportSelId:
        rc = timeline_report(&ctx, timelineFn, timelinePrefix, rptFn );
        break;

      case kAudioReportSelId:
        rc = audio_file_report(&ctx, audioFn, rptFn );
        break;

        
      default:
        rc = cmErrMsg(&ctx.err, kNoActionIdSelectedCtRC,"No action selector was selected.");

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
