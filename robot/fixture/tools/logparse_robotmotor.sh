#!/bin/bash
#cd "$(dirname ${BASH_SOURCE[0]})"

#sample log section: tread data
#[004.312580] LEFT tread test. power 127
#...
#[005.588429] RIGHT tread test. power 127
#...
#[006.737378] tread LEFT  FWD speed:+1958 avg:+1954 travel:+923
#[006.737563] tread LEFT  REV speed:-2039 avg:-2033 travel:-1149
#[006.737671] tread RIGHT FWD speed:+1969 avg:+1963 travel:+930
#[006.737774] tread RIGHT REV speed:-2036 avg:-2020 travel:-1139
#...
#[006.737879] LEFT tread test. power 75
#...
#[007.946988] RIGHT tread test. power 75
#...
#[009.161834] tread LEFT  FWD speed:+1118 avg:+1124 travel:+533
#[009.162009] tread LEFT  REV speed:-1141 avg:-1131 travel:-633
#[009.162120] tread RIGHT FWD speed:+1138 avg:+1127 travel:+534
#[009.162225] tread RIGHT REV speed:-1150 avg:-1137 travel:-638
#...

#sample log section: range data (lift head)
#[009.390339] LIFT range test. power 75
#...
#[010.251291] HEAD range test. power 100
#...
#[011.267224] LIFT POS start:1 passive:1 delta:0
#[011.267474] LIFT UP  speed:+2397 avg:+2233 travel:+598
#[011.267586] LIFT DN  speed:-2571 avg:-2289 travel:-562
#[011.267693] HEAD POS start:0 passive:0 delta:0
#[011.267800] HEAD UP  speed:+2884 avg:+2675 travel:+920
#[011.267906] HEAD DN  speed:-2966 avg:-2761 travel:-890
#...
#[011.268010] LIFT range test. power 50
#...
#[012.430094] HEAD range test. power 55
#...
#[014.003483] LIFT POS start:-128 passive:-128 delta:0
#[014.003741] LIFT UP  speed:+1540 avg:+1455 travel:+612
#[014.003858] LIFT DN  speed:-1548 avg:-1473 travel:-590
#[014.003966] HEAD POS start:-130 passive:-130 delta:0
#[014.004072] HEAD UP  speed:+1343 avg:+1295 travel:+804
#[014.004177] HEAD DN  speed:-1315 avg:-1293 travel:-780
#...

#cli usage: logparse_robotmotor [range] [tread]
gArgCnt=0; gDebug=0; gParseRange=0; gParseTread=0; gHelp=0;
for arg in "$@"; do
  if [ "$(echo ${arg} | xargs)" == "debug" ]; then gDebug=1; fi
  if [ "$(echo ${arg} | xargs)" == "range" ]; then gParseRange=1; fi
  if [ "$(echo ${arg} | xargs)" == "tread" ]; then gParseTread=1; fi
  #if [ "$(echo ${arg} | xargs)" == "--help" ]; then gHelp=1; fi
  if [ "$arg" == "--help" ]; then gHelp=1; fi
  if [ $gDebug -gt 0 ]; then echo arg[$gArgCnt] raw:\""${arg}"\"; fi # trimmed:\""$(echo ${arg} | xargs)"\"; fi
  gArgCnt=$((gArgCnt+1))
done

if [ $gDebug -gt 0 ]; then echo "gArgCnt=$gArgCnt gParseRange=$gParseRange gParseTread=$gParseTread gHelp=$gHelp"; fi
if [[ $gHelp -gt 0 || ( ! $gParseRange -gt 0 && ! $gParseTread -gt 0) ]]; then
  echo "Usage: logparse_robotmotor [OPTION]..."
  echo "BREIF: parses logfiles from ROBOT fixtures to collect motor test data"
  echo "Options:"
  echo "  --help     display this help message"
  echo "  range      parse range data for Head + Lift motors"
  echo "  tread      parse tread data for Left + Right treads"
  echo "  debug      enable debug printing"
  echo ""
  if [ $gHelp -gt 0 ]; then exit 0; fi
  if [ ! $gDebug -gt 0 ]; then exit 1; fi
fi

#globals
directory="$(dirname ${BASH_SOURCE[0]})"
filebase_range="robotmotor_range";
filebase_tread="robotmotor_tread";
gCurrentFile="";
gCurrentLine=0;

#---------------------------------------------------------------------------------------------------------------
#                       Range Stuffs (Lift,Head)
#---------------------------------------------------------------------------------------------------------------

#     offset: 0      1       2         3       4       5      6     7      8        9     10    11     12      13        14      15      16     17    18     19       20     21   22       23      24
rangeHeader1=("LIFT" "POS"   ""        ""      "SPEED" ""     "AVG" ""     "TRAVEL" ""     ""   "HEAD" "POS"   ""        ""      "SPEED" ""     "AVG" ""     "TRAVEL" ""     ""   ""       ""      "")
rangeHeader2=("pwr"  "start" "passive" "delta" "up"    "down" "up"  "down" "up"     "down" ""   "pwr"  "start" "passive" "delta" "up"    "down" "up"  "down" "up"     "down" ""   "result" "line#" "file")
declare -a rangeDatHigh;
declare -a rangeDatLow;
for val in "${rangeHeader2[@]}"; do rangeDatHigh+=(""); rangeDatLow+=(""); done
gRangeHigh=0; #dataset selector

function dRangeClear() { 
  if [ $gRangeHigh -gt 0 ]; then 
    for i in "${!rangeDatHigh[@]}"; do rangeDatHigh[$i]=""; done
    rangeDatHigh[10]=" ";
    rangeDatHigh[21]=" ";
  else
    for i in "${!rangeDatLow[@]}"; do rangeDatLow[$i]=""; done
    rangeDatLow[10]=" ";
    rangeDatLow[21]=" ";
  fi
}

function dRangeSetResult()  { local result=$1;                                 rangeDatHigh[22]=$result;       rangeDatLow[22]=$result; }
function dRangeSetLineNum() { local linenum=$1; if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[23]=$linenum; fi;  rangeDatLow[23]=$linenum; }
function dRangeSetFile()    { local fname=$1;   if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[24]=$fname;   fi;  rangeDatLow[24]=$fname; }
function dRangeSetPwrLift() { local pwr=$1;     if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[0]=$pwr;      else rangeDatLow[0]=$pwr; fi }
function dRangeSetPwrHead() { local pwr=$1;     if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[11]=$pwr;     else rangeDatLow[11]=$pwr; fi }

function dRangeSetPosLift() {
  local start=$1 passv=$2 delta=$3
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[1]=$start; else rangeDatLow[1]=$start; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[2]=$passv; else rangeDatLow[2]=$passv; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[3]=$delta; else rangeDatLow[3]=$delta; fi
}

function dRangeSetUpLift() {
  local speed=$1 spdavg=$2 travel=$3
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[4]=$speed;  else rangeDatLow[4]=$speed; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[6]=$spdavg; else rangeDatLow[6]=$spdavg; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[8]=$travel; else rangeDatLow[8]=$travel; fi
}

function dRangeSetDnLift() {
  local speed=$1 spdavg=$2 travel=$3
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[5]=$speed;  else rangeDatLow[5]=$speed; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[7]=$spdavg; else rangeDatLow[7]=$spdavg; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[9]=$travel; else rangeDatLow[9]=$travel; fi
}

function dRangeSetPosHead() {
  local start=$1 passv=$2 delta=$3
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[12]=$start; else rangeDatLow[12]=$start; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[13]=$passv; else rangeDatLow[13]=$passv; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[14]=$delta; else rangeDatLow[14]=$delta; fi
}

function dRangeSetUpHead() {
  local speed=$1 spdavg=$2 travel=$3
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[15]=$speed;  else rangeDatLow[15]=$speed; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[17]=$spdavg; else rangeDatLow[17]=$spdavg; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[19]=$travel; else rangeDatLow[19]=$travel; fi
}

function dRangeSetDnHead() {
  local speed=$1 spdavg=$2 travel=$3
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[16]=$speed;  else rangeDatLow[16]=$speed; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[18]=$spdavg; else rangeDatLow[18]=$spdavg; fi
  if [ $gRangeHigh -gt 0 ]; then rangeDatHigh[20]=$travel; else rangeDatLow[20]=$travel; fi
}

function dRangeWriteSel()
{
  local dsel=$1 row="" blanks=0 datary=()
  if [ $dsel -gt 0 ]; then datary=("${rangeDatHigh[@]}"); else datary=("${rangeDatLow[@]}"); fi
  
  for val in "${datary[@]}"; do
    row="$row,$val";
    if [ "$val" == "" ]; then blanks=$((blanks+1)); fi
  done
  
  if [ $dsel -gt 0 ]; then fappend="high"; else fappend="low"; fi
  outfile=$(printf "%s_%s.csv" "$filebase_range" "$fappend")
  
  if [ ! -e "$outfile" ]; then
    row1=""; row2=""; #first rows are column lables
    for val in "${rangeHeader1[@]}"; do row1="$row1,$val"; done
    for val in "${rangeHeader2[@]}"; do row2="$row2,$val"; done
    echo "$row1,ECHECK" >> $outfile
    echo "$row2,blanks" >> $outfile
  fi
  
  echo "$row,$blanks" >> $outfile
}
function dRangeWrite() { dRangeWriteSel "0"; dRangeWriteSel "1"; }

function RangeParseLine()
{
  local line=$1
  
  #strip timestamp prefix '[###.######] '
  #if [[ "$line" == *"LIFT"* ]] || [[ "$line" == *"HEAD"* ]]; then
  #  if [ "${line:0:1}" == "[" ]; then line=$(echo $line | grep -oP '\[[0-9]+\.[0-9]+\]\s\K(.)*'); fi
  #fi
  
  if [[ "$line" == *"LIFT"* ]]; then
    if [[ "$line" == *"LIFT range test"* ]]; then
      #NOTE: this is the first log line of any dataset. use to re-sync
      local power=$(echo $line | grep -oP 'power \K[+-]*([0-9]+)');
      if [ "$power" -ge 65 ]; then gRangeHigh=1; else gRangeHigh=0; fi
      if [ $gDebug -gt 0 ]; then echo "  found ds=$gRangeHigh"; fi
      dRangeClear
      dRangeSetPwrLift $power
      dRangeSetLineNum $gCurrentLine
      dRangeSetFile $gCurrentFile
    elif [[ "$line" == *"LIFT POS"* ]]; then
      local posStart=$(echo $line | grep -oP 'start:\K[+-]*([0-9]+)');
      local posPassv=$(echo $line | grep -oP 'passive:\K[+-]*([0-9]+)');
      local posDelta=$(echo $line | grep -oP 'delta:\K[+-]*([0-9]+)');
      dRangeSetPosLift $posStart $posPassv $posDelta
    elif [[ "$line" == *"LIFT UP"* ]]; then
      local speed=$(echo $line | grep -oP 'speed:\K[+-]*([0-9]+)');
      local spdavg=$(echo $line | grep -oP 'avg:\K[+-]*([0-9]+)');
      local travel=$(echo $line | grep -oP 'travel:\K[+-]*([0-9]+)');
      dRangeSetUpLift $speed $spdavg $travel
    elif [[ "$line" == *"LIFT DN"* ]]; then
      local speed=$(echo $line | grep -oP 'speed:\K[+-]*([0-9]+)');
      local spdavg=$(echo $line | grep -oP 'avg:\K[+-]*([0-9]+)');
      local travel=$(echo $line | grep -oP 'travel:\K[+-]*([0-9]+)');
      dRangeSetDnLift $speed $spdavg $travel
    fi
  
  elif [[ "$line" == *"HEAD"* ]]; then
    if [[ "$line" == *"HEAD range test"* ]]; then
      local power=$(echo $line | grep -oP 'power \K[+-]*([0-9]+)');
      dRangeSetPwrHead $power
    elif [[ "$line" == *"HEAD POS"* ]]; then
      local posStart=$(echo $line | grep -oP 'start:\K[+-]*([0-9]+)');
      local posPassv=$(echo $line | grep -oP 'passive:\K[+-]*([0-9]+)');
      local posDelta=$(echo $line | grep -oP 'delta:\K[+-]*([0-9]+)');
      dRangeSetPosHead $posStart $posPassv $posDelta
    elif [[ "$line" == *"HEAD UP"* ]]; then
      local speed=$(echo $line | grep -oP 'speed:\K[+-]*([0-9]+)');
      local spdavg=$(echo $line | grep -oP 'avg:\K[+-]*([0-9]+)');
      local travel=$(echo $line | grep -oP 'travel:\K[+-]*([0-9]+)');
      dRangeSetUpHead $speed $spdavg $travel
    elif [[ "$line" == *"HEAD DN"* ]]; then
      local speed=$(echo $line | grep -oP 'speed:\K[+-]*([0-9]+)');
      local spdavg=$(echo $line | grep -oP 'avg:\K[+-]*([0-9]+)');
      local travel=$(echo $line | grep -oP 'travel:\K[+-]*([0-9]+)');
      dRangeSetDnHead $speed $spdavg $travel
    fi
  
  elif [[ "$line" == *"[RESULT:"* ]]; then
    local result=$(echo "$line" | grep -oP 'RESULT:\K[0-9]+');
    dRangeSetResult $result
    if [ $gDebug -gt 0 ]; then echo "  writing result=$result"; fi
    dRangeWrite
    dRangeClear
  fi
}

#---------------------------------------------------------------------------------------------------------------
#                       Main
#---------------------------------------------------------------------------------------------------------------

gFileCnt=0; gLineCnt=0;
function parse_file()
{
  gCurrentFile=$1;
  gCurrentLine=0;
  
  dos2unix --quiet "$gCurrentFile"
  local numlines=$(wc -l < "$gCurrentFile")
  echo processing "$gCurrentFile" - $numlines lines
  
  while IFS='' read -r line || [[ -n "$line" ]]; do
    gCurrentLine=$((gCurrentLine+1))
    if [ $gParseRange -gt 0 ]; then RangeParseLine "$line"; fi
  done < "$gCurrentFile"
  
  gFileCnt=$(($gFileCnt+1))
  gLineCnt=$(($gLineCnt+$gCurrentLine))
}

#clean output files
rm -f $directory/$filebase_range*.csv $directory/$filebase_tread*.csv

#parse logfiles (*.log or *.txt formats)
Tstart=$(($(date +%s%N)/1000000))
for infile in ./*.log; do if [ "$infile" != "./*.log" ]; then parse_file "$infile"; fi done
for infile in ./*.txt; do if [ "$infile" != "./*.txt" ]; then parse_file "$infile"; fi done
Tend=$(($(date +%s%N)/1000000))
Tproc=$(($Tend-$Tstart))

echo processed $gFileCnt files $gLineCnt lines in $(($Tproc))ms. avg $(($Tproc/$gLineCnt))ms per line
exit 0

