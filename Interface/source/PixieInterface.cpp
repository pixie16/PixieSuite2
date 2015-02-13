#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <cstdlib>
#include <cstring>

#include <sys/time.h>

#include "pixie16app_export.h"

#include "Display.h"
#include "PixieInterface.h"

using namespace std;
using namespace Display;

set<string> PixieInterface::validConfigKeys;

// some simple histogram functions
PixieInterface::Histogram::Histogram() : error(NO_ERROR)
{
    bzero(data, sizeof(data));
}

PixieInterface::Histogram::Histogram(const PixieInterface::Histogram &x)
{
    memcpy(data, x.data, sizeof(data));
}

const PixieInterface::Histogram& PixieInterface::Histogram::operator=(const PixieInterface::Histogram &right)
{
    memcpy(data, right.data, sizeof(data));

    return *this;
}

PixieInterface::Histogram PixieInterface::Histogram::operator+(const PixieInterface::Histogram &right)
{
    Histogram x(*this);

    for (size_t i=0; i < HISTO_SIZE; i++) {
	x.data[i] += right.data[i];
    }
    
    return x;
}

PixieInterface::Histogram PixieInterface::Histogram::operator-(const PixieInterface::Histogram &right)
{
    Histogram x(*this);
    
    for (size_t i=0; i < HISTO_SIZE; i++) {
	if (x.data[i] < right.data[i]) {
	    x.data[i] = 0;
	    error = ERROR_SUBTRACT;
	} else {
	    x.data[i] -= right.data[i];
	}
    }

    return x;
}

const PixieInterface::Histogram& PixieInterface::Histogram::operator+=(const PixieInterface::Histogram &right)
{
    return (*this = *this + right);
}

const PixieInterface::Histogram& PixieInterface::Histogram::operator-=(const PixieInterface::Histogram &right)
{
    return (*this = *this - right);
}

bool PixieInterface::Histogram::Read(PixieInterface &pif, unsigned int mod, unsigned int ch)
{
    if (pif.ReadHistogram(data, PixieInterface::HISTO_SIZE, mod, ch))
	return true;
    error = ERROR_READ;
    return false;
}

bool PixieInterface::Histogram::Write(ofstream &out) 
{
    out.write((char*)data, sizeof(data));

    if (!out.good()) {
	error = ERROR_WRITE;
	return false;
    }
    return true;
}

PixieInterface::PixieInterface(const char *fn) : hasAlternativeConfig(false), lock("PixieInterface")
{
  SetColorTerm();
  // Set-up valid configuration keys if they don't exist yet
  if (validConfigKeys.empty()) { 
    //? perhaps these should allow more than just one alternate firmware configuration 
    validConfigKeys.insert("AltComFpgaFile");
    validConfigKeys.insert("AltDspConfFile");
    validConfigKeys.insert("AltDspVarFile");
    validConfigKeys.insert("AltSpFpgaFile");
    validConfigKeys.insert("AltTrigFpgaFile");
    // standard files
    validConfigKeys.insert("ComFpgaFile");
    validConfigKeys.insert("DspConfFile");
    validConfigKeys.insert("DspVarFile");
    validConfigKeys.insert("DspSetFile");
    validConfigKeys.insert("DspWorkingSetFile");
    validConfigKeys.insert("ListModeFile");
    validConfigKeys.insert("PixieBaseDir");
    validConfigKeys.insert("SlotFile");
    validConfigKeys.insert("SpFpgaFile");
    validConfigKeys.insert("TrigFpgaFile");
  }
  if (!ReadConfigurationFile(fn)) {
    cout << ErrorStr("Error reading configuration file") << endl;
    exit(EXIT_FAILURE);
  }

  for (unsigned int i=0; i < MAX_MODULES; i++) {
    hasExtra[i] = false;
    extraWord[i] = 0;
  }
}

PixieInterface::~PixieInterface()
{
  if (!doneInit)
    return;

  if (CheckRunStatus())
    EndRun();

  LeaderPrint("Closing Pixie interface");

  retval = Pixie16ExitSystem(numberCards);
  CheckError();
}

bool PixieInterface::ReadConfigurationFile(const char *fn) 
{
  ifstream in(fn);
  if (!in)    
    return false;

  stringbuf tag, value;
  string line;

	//Loop over lines in config file
  while (std::getline(in,line)) {
		//Get a string stream of current line
	  std::istringstream lineStream(line);
		//If the line leads with a '#' we ignore it.
	  if (lineStream.peek() == '#') continue;

		//Extract the tag and value
	  std::string tag,value;
	  if ((lineStream >> tag >> value)) {
			//Check if tag is recognized
		  if (validConfigKeys.find(tag) == validConfigKeys.end()) {
			  cout << "Unrecognized tag " << WarningStr(tag) << " in PixieInterface configuration file." << endl;
		  }
			
			//Store configuration
		  configStrings[tag] = ConfigFileName(value);

			//Check if BaseDir is defined differently then in the environment
		  if (tag == "PixieBaseDir") {
			  cout << "Pixie base directory is " << InfoStr(value) << endl;
			  // check if this matches the environment PXI_ROOT if it is set
			  if (getenv("PXI_ROOT") != NULL) {
				  if ( value != string(getenv("PXI_ROOT")) ) {
					  cout << WarningStr("This does not match the value of PXI_ROOT set in the environment") << endl;
				  }
			  }
		  }
	  }
  }

  return true;
}

bool PixieInterface::GetSlots(const char *slotF)
{
  char restOfLine[CONFIG_LINE_LENGTH];

  if (slotF == NULL)
    slotF = configStrings["SlotFile"].c_str();

  ifstream in(slotF);

  if (!in) {
    cout << ErrorStr("Error opening slot definition file: ") << ErrorStr(slotF) << endl;
    exit(EXIT_FAILURE);
  }
  stringstream line;

  in >> numberCards;
  in.getline(restOfLine, CONFIG_LINE_LENGTH, '\n');

  if (numberCards > MAX_MODULES) {
    cout << ErrorStr("Too many cards") << " : " << numberCards << " > " << MAX_MODULES << endl;
    exit(EXIT_FAILURE);
  }

  for (int i = 0; i < numberCards; i++) {
    // check if this is a module with an alternative firmware configuration (tagged with '*')
    if (in.peek() == '*') {
      in.ignore();
      hasAlternativeConfig = true;
      firmwareConfig[i] = 1; // alternative config
    } else firmwareConfig[i] = 0; // standard config

    in >> slotMap[i];
    in.getline(restOfLine, CONFIG_LINE_LENGTH, '\n');
    if (!in.good()) {
      cout << ErrorStr("Error reading slot definition file.") << endl;
      exit(EXIT_FAILURE);
    }
  }

  cout << "Slot definition from " << InfoStr(slotF) << endl;
  cout << "  System with " << numberCards << " cards" << endl;
  cout << "  ";

  for (int i = 0; i < numberCards; i++) {
    cout << "||  M  S ";
  }
  cout << "|" << endl << "  ";

  for (int i = 0; i < numberCards; i++) {
    cout << "|| " << setw(2) << i << " " << setw(2) << slotMap[i] << " ";
  }
  cout << "|" << endl;
  in.close();

  return true;
}

bool PixieInterface::Init(bool offlineMode)
{
  LeaderPrint("Initializing Pixie");

  retval = Pixie16InitSystem(numberCards, slotMap, offlineMode);
  doneInit = !CheckError(true);

  return doneInit;
}

bool PixieInterface::Boot(int mode, bool useWorkingSetFile) 
{
  string &setFile = useWorkingSetFile ? 
    configStrings["DspWorkingSetFile"] : configStrings["DspSetFile"];

  LeaderPrint("Booting Pixie");

 bool goodBoot = true;

  if (hasAlternativeConfig) {
    // must proceed through boot module by module
    cout << InfoStr("[MULTICONFIG]");
    for (int i=0; i < numberCards; i++) {
      if (firmwareConfig[i] == 1) {
	// use the Alt... files
	retval = Pixie16BootModule(&configStrings["AltComFpgaFile"][0],
				   &configStrings["AltSpFpgaFile"][0],
				   &configStrings["AltTrigFpgaFile"][0],
				   &configStrings["AltDspConfFile"][0],
				   &setFile[0],
				   &configStrings["AltDspVarFile"][0],
				   i, mode);
      } else {
	// use the standard files
	retval = Pixie16BootModule(&configStrings["ComFpgaFile"][0], 
				   &configStrings["SpFpgaFile"][0], 
				   &configStrings["TrigFpgaFile"][0],
				   &configStrings["DspConfFile"][0], 
				   &setFile[0],
				   &configStrings["DspVarFile"][0],
				   i, mode);
      }
      goodBoot = (goodBoot && !CheckError(true));
    }
  } else {
    // boot all at once
    retval = Pixie16BootModule(&configStrings["ComFpgaFile"][0], 
			       &configStrings["SpFpgaFile"][0], 
			       &configStrings["TrigFpgaFile"][0],
			       &configStrings["DspConfFile"][0], 
			       &setFile[0],
			       &configStrings["DspVarFile"][0],
			       numberCards, mode);
    goodBoot = !CheckError(true);
  }
 
  cout << "  Used set file: " << InfoStr(setFile) << endl;

  LeaderPrint("Checking SlotIDs");

  bool hadError = false;
  bool updated = false;

  word_t val;

  for (int i=0; i < numberCards; i++) {
    if (!ReadSglModPar("SlotID", &val, i))
      hadError = true;
    if (val != slotMap[i]) {
      updated = true;
      if (!WriteSglModPar("SlotID", slotMap[i], i))
	hadError = true;
    }
  }
  if (hadError) 
    cout << ErrorStr() << endl;
  else if (updated)
    cout << WarningStr("[UPDATED]") << endl;
  else
    cout << OkayStr() << endl;

  return goodBoot && !hadError;
}

bool PixieInterface::WriteSglModPar(const char *name, word_t val, int mod)
{
  strncpy(tmpName, name, nameSize);

  retval = Pixie16WriteSglModPar(tmpName, val, mod);
  if (retval < 0) {
    cout << "Error writing module parameter " << WarningStr(name) 
	 << " for module " << mod << endl;
    return false;      
  }
  return true;
}

bool PixieInterface::ReadSglModPar(const char *name, word_t *val, int mod)
{
  strncpy(tmpName, name, nameSize);

  retval = Pixie16ReadSglModPar(tmpName, val, mod);
  if (retval < 0) {
    cout << "Error reading module parameter " << WarningStr(name) 
	 << " for module " << mod << endl;
    return false;      
  }
  return true;
}

void PixieInterface::PrintSglModPar(const char *name, int mod)
{
  word_t val;

  strncpy(tmpName, name, nameSize);

  if (ReadSglModPar(tmpName, &val, mod)) {    
	cout.unsetf(ios_base::floatfield);
    cout << "  MOD " << setw(2) << mod 
	 << "  " << setw(15) << name
	 << "  " << setprecision(6) << val << endl;
  }
}

bool PixieInterface::WriteSglChanPar(const char *name, double val, int mod, int chan)
{
  strncpy(tmpName, name, nameSize);

  retval = Pixie16WriteSglChanPar(tmpName, val, mod, chan);
  if (retval < 0) {
    cout << "Error writing channel parameter " << WarningStr(name) 
	 << " for module " << mod << ", channel " << chan << endl;
    return false;      
  }
  return true;
}

bool PixieInterface::ReadSglChanPar(const char *name, double *pval, int mod, int chan)
{
  strncpy(tmpName, name, nameSize);

  retval = Pixie16ReadSglChanPar(tmpName, pval, mod, chan);
  if (retval < 0) {
    cout << "Error reading channel parameter " << WarningStr(name) 
	 << " for module " << mod << ", channel " << chan << endl;
    return false;      
  }
  return true;
}

void PixieInterface::PrintSglChanPar(const char *name, int mod, int chan)
{
  double val;
  strncpy(tmpName, name, nameSize);

  if (ReadSglChanPar(tmpName, &val, mod, chan)) {    
	cout.unsetf(ios_base::floatfield);
    cout << "  MOD " << setw(2) << mod 
	 << "  CHAN " << setw(2) << chan
	 << "  " << setw(15) << name
	 << "  " << setprecision(6) << val << endl;
  }
}

bool PixieInterface::SaveDSPParameters(const char *fn)
{
  if (fn == NULL)
    fn = &configStrings["DspWorkingSetFile"][0];
  strncpy(tmpName, fn, nameSize);

  LeaderPrint("Writing DSP parameters");

  retval = Pixie16SaveDSPParametersToFile(tmpName);
  return !CheckError();
}

bool PixieInterface::AcquireTraces(int mod)
{
  retval = Pixie16AcquireADCTrace(mod);

  if (retval < 0) {
    cout << ErrorStr("Error acquiring ADC traces from module ") << mod << endl;
    return false;
  }

  return true;
}

bool PixieInterface::ReadSglChanTrace(unsigned short *buf, unsigned long sz,
				      unsigned short mod, unsigned short chan)
{
  if (sz > TRACE_LENGTH) {
    cout << ErrorStr("Trace length too large.") << endl;
    return false;
  }

  retval = Pixie16ReadSglChanADCTrace(buf, sz, mod, chan);

  if (retval < 0) {
    cout << ErrorStr("Error reading trace in module ") << mod << endl;
    return false;
  }

  return true;
}

bool PixieInterface::GetStatistics(unsigned short mod)
{
  retval = Pixie16ReadStatisticsFromModule(statistics, mod);

  if (retval < 0) {
    cout << WarningStr("Error reading statistics from module ") << mod << endl;
    return false;
  } 

  return true;
}

double PixieInterface::GetInputCountRate(int mod, int chan)
{
  return Pixie16ComputeInputCountRate(statistics,mod,chan);
}

double PixieInterface::GetOutputCountRate(int mod, int chan)
{
  return Pixie16ComputeOutputCountRate(statistics,mod,chan);
}

double PixieInterface::GetLiveTime(int mod, int chan)
{
  return Pixie16ComputeLiveTime(statistics,mod,chan);
}

double PixieInterface::GetRealTime(int mod)
{
  return Pixie16ComputeRealTime(statistics,mod);
}

double PixieInterface::GetProcessedEvents(int mod)
{
  return Pixie16ComputeProcessedEvents(statistics,mod);
}

bool PixieInterface::StartHistogramRun(unsigned short mode)
{
  LeaderPrint("Starting histogram run");
  retval = Pixie16StartHistogramRun(numberCards,mode);

  return !CheckError();
}

bool PixieInterface::StartHistogramRun(unsigned short mod, unsigned short mode)
{
  retval = Pixie16StartHistogramRun(mod,mode);

  if (retval < 0) {
    cout << ErrorStr("Error starting histogram run in module ") << mod << endl;
    exit(EXIT_FAILURE);
  }
  return true;
}

bool PixieInterface::StartListModeRun(unsigned short listMode,
				      unsigned short runMode)
{
  LeaderPrint("Starting list mode run");
  retval = Pixie16StartListModeRun(numberCards, listMode, runMode);

  return !CheckError();
}

bool PixieInterface::StartListModeRun(unsigned short mod, 
				      unsigned short listMode,
				      unsigned short runMode)
{
  retval = Pixie16StartListModeRun(mod, listMode, runMode);
  
  if (retval < 0) {
    cout << ErrorStr("Error starting list mode run in module ") << mod << endl;
    exit(EXIT_FAILURE);
  }

  return true;
}

bool PixieInterface::CheckRunStatus()
{
  for (int mod = 0; mod < numberCards; mod++) {
    if (!CheckRunStatus(mod))
      return false;
  }

  return true;
}

bool PixieInterface::CheckRunStatus(int mod)
{
  retval = Pixie16CheckRunStatus(mod);
  
  if (retval < 0) {
    cout << WarningStr("Error checking run status in module ") << mod << endl;
  }

  return (retval == 1);
}


// only Rev. D has the external FIFO
#ifdef PIF_FIFO
unsigned long PixieInterface::CheckFIFOWords(unsigned short mod)
{
  // word_t nWords;
  unsigned int nWords;

  retval = Pixie16CheckExternalFIFOStatus(&nWords, mod);

  if (retval < 0) {
    cout << WarningStr("Error checking FIFO status in module ") << mod << endl;
    return 0;
  }
 
  return nWords + (hasExtra[mod] ? 1 : 0);
}

bool PixieInterface::ReadFIFOWords(word_t *buf, unsigned long nWords,
				   unsigned short mod)
{
  if (nWords == 1) {
    if (hasExtra[mod]) {
      *buf = extraWord[mod];
      hasExtra[mod] = false;
      return true;
    }
    word_t minibuf[2];

    retval = Pixie16ReadDataFromExternalFIFO(minibuf, 2, mod);
    *buf = minibuf[0];
    extraWord[mod] = minibuf[1];
    hasExtra[mod] = true;
  } else {
    if (hasExtra[mod]) {
      *buf++ = extraWord[mod];
      hasExtra[mod] = false;
      nWords--;
    }
    retval = Pixie16ReadDataFromExternalFIFO(buf, nWords, mod);
  }

  if (retval < 0) {
    cout << WarningStr("Error reading words from FIFO in module ") << mod << endl;
    return false;
  }

  return true;
}
#endif // Rev. D FIFO access

bool PixieInterface::EndRun()
{
  bool b = true;

  LeaderPrint("Ending run");

  for (int mod=0; mod < numberCards; mod++)
    if (!EndRun(mod))
      b = false;

  if (!b) {
    cout << ErrorStr() << endl;
  } else {
    cout << OkayStr() << endl;
  }

  return b;
}

bool PixieInterface::EndRun(int mod)
{
  retval = Pixie16EndRun(mod);

  if (retval < 0) {
    cout << WarningStr("Failed to end run in module ") << mod << endl;
    return false;
  }

  return true;
}

bool PixieInterface::RemovePresetRunLength(int mod)
{
  strncpy(tmpName, "HOST_RT_PRESET", nameSize);

  unsigned long bigVal = Decimal2IEEEFloating(99999);

  LeaderPrint("Removing preset run length");
  
  if (!WriteSglModPar(tmpName, bigVal, mod)) {
    cout << ErrorStr() << endl;
    return false;
  }
  cout << OkayStr() << endl;
  return true;
}

bool PixieInterface::ReadHistogram(word_t *hist, unsigned long sz,
				   unsigned short mod, unsigned short ch)
{
  if (sz > MAX_HISTOGRAM_LENGTH) {
    cout << ErrorStr("Histogram length is too large.") << endl;
    return false;
  }

  retval = Pixie16ReadHistogramFromModule(hist, sz, mod, ch);
  
  if (retval < 0) {
    cout << ErrorStr("Failed to get histogram data from module ") << mod << endl;
    return false;
  }
  return true;
}

bool PixieInterface::AdjustOffsets(unsigned short mod)
{
  LeaderPrint("Adjusting Offsets");
  retval = Pixie16AdjustOffsets(mod);

  return !CheckError();
}

bool PixieInterface::ToggleGain(int mod, int chan)
{
  return ToggleChannelBit(mod, chan, "CHANNEL_CSRA", CCSRA_ENARELAY);
}

bool PixieInterface::ToggleGood(int mod, int chan)
{
  return ToggleChannelBit(mod, chan, "CHANNEL_CSRA", CCSRA_GOOD);
}

bool PixieInterface::TogglePolarity(int mod, int chan)
{
  return ToggleChannelBit(mod, chan, "CHANNEL_CSRA", CCSRA_POLARITY);
}

#ifdef PIF_CATCHER
bool PixieInterface::ToggleCatcherBit(int mod, int chan)
{
  CatcherMessage();
  return ToggleChannelBit(mod, chan, "CHANNEL_CSRA", CCSRA_CATCHER);
}

bool PixieInterface::TogglePileupBit(int mod, int chan)
{
  return ToggleChannelBit(mod, chan, "CHANNEL_CSRA", CCSRA_PILEUP);
}

bool PixieInterface::ToggleTraceCapture(int mod, int chan)
{
  return ToggleChannelBit(mod, chan, "CHANNEL_CSRA", CCSRA_TRACEENA);
}

bool PixieInterface::SetProtonCatcherMode(int mod, int chan, CatcherModes mode)
{
  double dval;

  CatcherMessage();

  ReadSglChanPar("CHANNEL_CSRA", &dval, mod, chan);
  unsigned int ival = (int)dval;

  switch (mode) {
  case PC_STANDARD:
  case PC_HYBRID:
    ival = APP32_ClrBit(CCSRA_PILEUP, ival);
    break;
  case PC_REJECT:
  case PC_ACCEPT:
    ival = APP32_SetBit(CCSRA_PILEUP, ival);
    break;
  }
  switch (mode) {
  case PC_STANDARD:
  case PC_REJECT:
    ival = APP32_ClrBit(CCSRA_CATCHER, ival);
    break;
  case PC_HYBRID:
  case PC_ACCEPT:
    ival = APP32_SetBit(CCSRA_CATCHER, ival);
    break;
  }
  dval = ival;

  return WriteSglChanPar("CHANNEL_CSRA", dval, mod, chan);
}

void PixieInterface::CatcherMessage(void)
{
  // Only do this message once per program execution
  static bool threwMessage = false;

  if (false)
    cout << WarningStr("Altering firmware dependent bit") << endl;
  threwMessage = true;
}
#endif

// ### PRIVATE FUNCTIONS BELOW ### //
bool PixieInterface::ToggleChannelBit(int mod, int chan, const char *parameter, int bit)
{
  double dval;

  ReadSglChanPar(parameter, &dval, mod, chan);
  unsigned int ival = (int)dval;
  ival ^= (1 << bit);
  dval = ival;

  return WriteSglChanPar(parameter, dval, mod, chan);
}

string PixieInterface::ConfigFileName(const string &str) 
{
  if (str[0] == '.' || str[0] == '/')
    return str;
  else
    return configStrings["PixieBaseDir"] + '/' + str;
}

bool PixieInterface::CheckError(bool exitOnError) const
{
  if (StatusPrint(retval < 0) && exitOnError) {
    exit(EXIT_FAILURE); // or do something else
  }

  return (retval < 0);
}