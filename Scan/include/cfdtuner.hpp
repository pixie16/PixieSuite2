#ifndef CFDTUNER_HPP
#define CFDTUNER_HPP

#include <ctime>
#include <vector>
#include <cmath>

#include "Unpacker.hpp"

class ChannelEvent;

class CfdTuner : public Unpacker{
  private:
	std::vector<int> x_vals;
	
	time_t last_trace; ///< The time of the last trace.
  
	unsigned int num_traces; ///< The total number of traces.
	
	unsigned int num_processed; ///< The number of processed traces.
	
	std::vector<ChannelEvent*> events_; ///<The buffer of waveforms to be plotted.

	/// Process all events in the event list.
	void ProcessRawEvent();
	
	/// Clear any stored waveforms.
	void ClearEvents();
	
  public:
	CfdTuner();
	
	~CfdTuner();
	
	bool Initialize(std::string prefix_="");

	/// Return the syntax string for this program.
	void SyntaxStr(const char *name_, std::string prefix_=""){ std::cout << prefix_ << "SYNTAX: " << std::string(name_) << " <options> <input>\n"; }
	
	/// Print a command line help dialogue for recognized command line arguments.
	void ArgHelp(std::string prefix_="");
	
	/// Scan input arguments and set class variables.
	bool SetArgs(std::deque<std::string> &args_, std::string &filename_);

	/// Print a status message.	
	void PrintStatus(std::string prefix_=""){ std::cout << prefix_ << "Found " << num_traces << " traces and processed " << num_processed << ".\n"; }
};

/// Return a pointer to a new CfdTuner object.
Unpacker *GetCore(){ return (Unpacker*)(new CfdTuner()); }

#endif
