#include <iostream>
#include <algorithm> 

// PixieCore libraries
#include "ScanMain.hpp"
#include "PixieEvent.hpp"

// Local files
#include "cfdtuner.hpp"

// Root files

#define ADC_TIME_STEP 4 // ns

CfdTuner::CfdTuner() :
	num_traces(0),
	num_processed(0)
{
}

CfdTuner::~CfdTuner(){
	// Call Unpacker::Close() to finish cleanup.
	Close();
}

void CfdTuner::ClearEvents() {
	while (!events_.empty()) {
		delete events_.back();
		events_.pop_back();
	}
}

void CfdTuner::ProcessRawEvent(){
	PixieEvent *current_event = NULL;

	// Fill the processor event deques with events
	while(!rawEvent.empty()){
		if(kill_all){ break; }
		//Get the first event int he FIFO.
		current_event = rawEvent.front();
		rawEvent.pop_front();

		// Safety catches for null event or empty adcTrace.
		if(!current_event || current_event->adcTrace.empty()){
			continue;
		}

		ChannelEvent *channel_event = new ChannelEvent(current_event);

		//Process the waveform.
		//channel_event->FindLeadingEdge();
		channel_event->CorrectBaseline();
		channel_event->FindQDC();

		//Store the waveform in the stack of waveforms to be displayed.
		events_.push_back(channel_event);
		
		num_traces++;
	}
	
	// Do something with the pulses here...
	//num_processed++;
	
	// Clear all events from the event vector.
	ClearEvents();
}

bool CfdTuner::Initialize(std::string prefix_){
	if(init){ return false; }

	// Print a small welcome message.
	std::cout << prefix_ << "Welcome to this program! Usually I would say something useful here.\n";

	return (init = true);
}

/**
 * \param[in] prefix_
 */
void CfdTuner::ArgHelp(std::string prefix_){
	std::cout << prefix_ << "--opt1 | Blah blah blah.\n";
	std::cout << prefix_ << "--opt2 | Blah blah blah.\n";
}

/**
 * \param args_
 * \param filename_
 */
bool CfdTuner::SetArgs(std::deque<std::string> &args_, std::string &filename_){
	std::string current_arg;
	while(!args_.empty()){
		current_arg = args_.front();
		args_.pop_front();

		if(current_arg == "--opt1"){
			// Do something with option1.
			args_.pop_front();
		}
		else if(current_arg == "--opt2"){
			// Do something with option2.
			args_.pop_front();
		}
		else{ filename_ = current_arg; }
	}
	
	return true;
}

int main(int argc, char *argv[]){
	// Initialize a new Unpacker object.
	ScanMain scan_main((Unpacker*)(new CfdTuner()));
	
	// Force batch mode. Needed if we're going
	// to call execute multiple times.
	scan_main.SetBatchMode();
	
	// Initialize the scanner.
	scan_main.Initialize(argc, argv);
	
	// Set the system message header.
	scan_main.SetMessageHeader("CfdTuner: ");

	// Scan the file a specified number of times.
	for(int i = 0; i < 1; i++){
		scan_main.Execute();
	}

	return 0;
}
