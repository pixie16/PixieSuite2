#include <iostream>
#include <algorithm> 

// PixieCore libraries
#include "ScanMain.hpp"
#include "PixieEvent.hpp"

// Local files
#include "skeleton.hpp"

// Root files
#include "TApplication.h"
#include "TSystem.h"
#include "TCanvas.h"
#include "TGraph.h"
#include "TAxis.h"
#include "TFile.h"

#define ADC_TIME_STEP 4 // ns
#define SLEEP_WAIT 1E4 // When not in shared memory mode, length of time to wait between calls to gSystem->ProcessEvents (in us).

Skeleton::Skeleton(int mod/*= 0*/, int chan/*=0*/, bool draw/*=false*/) :
	mod_(mod),
	chan_(chan), 
	acqRun_(true),
	singleCapture_(false),
	threshLow_(0),
	threshHigh_(-1),
	delay_(2),
	do_drawing_(draw),
	num_traces(0),
	num_displayed(0)
{
	if(do_drawing_){
		time(&last_trace);
		
		// Variables for root graphics
		rootapp = new TApplication("scope", 0, NULL);
		gSystem->Load("libTree");
	
		canvas = new TCanvas("scope_canvas", "Skeleton");
		canvas->cd();
	
		graph = new TGraph();
		graph->SetMarkerStyle(kFullDotSmall);
	}
}

Skeleton::~Skeleton(){
	if(do_drawing_){
		canvas->Close();
		delete canvas;
		delete graph;
	}

	// Call Unpacker::Close() to finish cleanup.
	Close();
}


void Skeleton::ResetGraph(unsigned int size) {
	if(size != x_vals.size()){
		std::cout << message_head << "Changing trace length from " << x_vals.size()*ADC_TIME_STEP << " to " << size*ADC_TIME_STEP << " ns.\n";
		x_vals.resize(size);
		for(size_t index = 0; index < x_vals.size(); index++){
			x_vals[index] = ADC_TIME_STEP * index;
		}	
		while (do_drawing_ && (unsigned int) graph->GetN() > size) graph->RemovePoint(graph->GetN());
	}

	if(do_drawing_){
		std::stringstream stream;
		stream << "M" << mod_ << "C" << chan_;
		graph->SetTitle(stream.str().c_str());
	}

	resetGraph_ = false;
}

void Skeleton::Plot(ChannelEvent *event){
	if(!do_drawing_){ return; }

	///The limits of the vertical axis
	static float axisVals[2][2]; //The max and min values of the graph, first index is the axis, second is the min / max
	static float userZoomVals[2][2];
	static bool userZoom[2];

	//Get the user zoom settings.
	userZoomVals[0][0] = canvas->GetUxmin();
	userZoomVals[0][1] = canvas->GetUxmax();
	userZoomVals[1][0] = canvas->GetUymin();
	userZoomVals[1][1] = canvas->GetUymax();

	if(event->size != x_vals.size()){ // The length of the trace has changed.
		resetGraph_ = true;
	}
	if (resetGraph_) {
		ResetGraph(event->size);
		for (int i=0;i<2;i++) {
			axisVals[i][0] = 1E9;
			axisVals[i][1] = -1E9;
			userZoomVals[i][0] = 1E9;
			userZoomVals[i][1] = -1E9;
			userZoom[i] = false;
		}
	}

	//Determine if the user had zoomed or unzoomed.
	for (int i=0;i<2;i++) {
		userZoom[i] =  (userZoomVals[i][0] != axisVals[i][0] || userZoomVals[i][1] != axisVals[i][1]);
	}

	int index = 0;
	for (size_t i=0;i<event->size;++i) {
		graph->SetPoint(index, x_vals[i], event->event->adcTrace[i]);
		index++;
	}

	//Get and set the updated graph limits.
	if (graph->GetXaxis()->GetXmin() < axisVals[0][0]) axisVals[0][0] = graph->GetXaxis()->GetXmin(); 
	if (graph->GetXaxis()->GetXmax() > axisVals[0][1]) axisVals[0][1] = graph->GetXaxis()->GetXmax(); 
	graph->GetXaxis()->SetLimits(axisVals[0][0], axisVals[0][1]);
	if (graph->GetYaxis()->GetXmin() < axisVals[1][0]) axisVals[1][0] = graph->GetYaxis()->GetXmin(); 
	if (graph->GetYaxis()->GetXmax() > axisVals[1][1]) axisVals[1][1] = graph->GetYaxis()->GetXmax(); 
	graph->GetYaxis()->SetLimits(axisVals[1][0], axisVals[1][1]);

	//Set the users zoom window.
	for (int i=0;i<2;i++) {
		if (!userZoom[i]) {
			for (int j=0;j<2;j++) userZoomVals[i][j] = axisVals[i][j];
		}
	}
	graph->GetXaxis()->SetRangeUser(userZoomVals[0][0], userZoomVals[0][1]);
	graph->GetYaxis()->SetRangeUser(userZoomVals[1][0], userZoomVals[1][1]);

	graph->Draw("AP0");

	canvas->Update();

	if (saveFile_ != "") {
		TFile f(saveFile_.c_str(), "RECREATE");
		graph->Clone("trace")->Write();
		f.Close();
		saveFile_ = "";
	}

	num_displayed++;
}

void Skeleton::ClearEvents() {
	while (!events_.empty()) {
		delete events_.back();
		events_.pop_back();
	}
}

void Skeleton::ProcessRawEvent(){
	PixieEvent *current_event = NULL;

	// Fill the processor event deques with events
	while(!rawEvent.empty()){
		if(kill_all){ break; }
		//If the acquistion is not running we clear the events.
		//	For the SHM mode we simply break and wait for the next data packet.
		//	For the ldf mode we sleep and check acqRun again.
		while(!acqRun_) {
			//Clean up any stored waveforms.
			ClearEvents();
			if(shm_mode || kill_all){ return; }
			usleep(SLEEP_WAIT);
			IdleTask();
		}

		//Check if we have delayed the plotting enough
		time_t cur_time;
		time(&cur_time);
		while(difftime(cur_time, last_trace) < delay_) {
			//If in shm mode and the plotting time has not alloted the events are cleared and this function is aborted.
			if (shm_mode) {
				ClearEvents();
				return;
			}
			else {
				usleep(SLEEP_WAIT);
				IdleTask();
				time(&cur_time);
			}
		}	


		//Get the first event int he FIFO.
		current_event = rawEvent.front();
		rawEvent.pop_front();

		// Safety catches for null event or empty adcTrace.
		if(!current_event || current_event->adcTrace.empty()){
			continue;
		}

		// Pass this event to the correct processor
		int maximum = *std::max_element(current_event->adcTrace.begin(),current_event->adcTrace.end());
		if(current_event->modNum == mod_ && current_event->chanNum == chan_){  
			num_traces++;
			// Check low threshold.
			if (maximum < threshLow_) {
				delete current_event;
				continue;
			}
			// Check high threhsold.
			if (threshHigh_ > threshLow_ && maximum > threshHigh_) {
				delete current_event;
				continue;
			}

			ChannelEvent *channel_event = new ChannelEvent(current_event);

			//Process the waveform.
			//channel_event->FindLeadingEdge();
			channel_event->CorrectBaseline();
			channel_event->FindQDC();

			//Store the waveform in the stack of waveforms to be displayed.
			events_.push_back(channel_event);
		}
		IdleTask();
	}
	
	// Do something with the pulses here...
	
	// For example, if we wanted to plot these traces.
	if(do_drawing_){
		for(std::vector<ChannelEvent*>::iterator iter = events_.begin(); iter != events_.end(); iter++){
			Plot(*iter); 
		
			time(&last_trace);
		}
	}
	
	// Clear all events from the event vector.
	ClearEvents();
}

bool Skeleton::Initialize(std::string prefix_){
	if(init){ return false; }

	// Print a small welcome message.
	std::cout << prefix_ << "Displaying traces for mod = " << mod_ << ", chan = " << chan_ << ".\n";

	return (init = true);
}

/**
 * \param[in] prefix_
 */
void Skeleton::ArgHelp(std::string prefix_){
	std::cout << prefix_ << "--mod [module]   | Module of signal of interest (default=0)\n";
	std::cout << prefix_ << "--chan [channel] | Channel of signal of interest (default=0)\n";
}

/** 
 *
 *	\param[in] prefix_ 
 */
void Skeleton::CmdHelp(std::string prefix_){
	/*std::cout << prefix_ << "set <module> <channel> - Set the module and channel of signal of interest (default = 0, 0).\n";
	std::cout << prefix_ << "stop                   - Stop the acquistion.\n";
	std::cout << prefix_ << "run                    - Run the acquistion.\n";
	std::cout << prefix_ << "single                 - Perform a single capture.\n";
	std::cout << prefix_ << "thresh <low> [high]    - Set the plotting window for trace maximum.\n";
	std::cout << prefix_ << "fit <low> <high>       - Turn on fitting of waveform.\n";
	std::cout << prefix_ << "avg <numWaveforms>     - Set the number of waveforms to average.\n";
	std::cout << prefix_ << "save <fileName>        - Save the next trace to the specified file name..\n";
	std::cout << prefix_ << "delay [time]           - Set the delay between drawing traces (in seconds, default = 1 s).\n";
	std::cout << prefix_ << "log                    - Toggle log/linear mode on the y-axis.\n";*/
}

/**
 * \param args_
 * \param filename_
 */
bool Skeleton::SetArgs(std::deque<std::string> &args_, std::string &filename_){
	std::string current_arg;
	while(!args_.empty()){
		current_arg = args_.front();
		args_.pop_front();

		if(current_arg == "--mod"){
			if(args_.empty()){
				std::cout << " Error: Missing required argument to option '--mod'!\n";
				return false;
			}
			mod_ = atoi(args_.front().c_str());
			args_.pop_front();
		}
		else if(current_arg == "--chan"){
			if(args_.empty()){
				std::cout << " Error: Missing required argument to option '--chan'!\n";
				return false;
			}
			chan_ = atoi(args_.front().c_str());
			args_.pop_front();
		}
		else{ filename_ = current_arg; }
	}
	
	return true;
}

bool Skeleton::CommandControl(std::string cmd_, const std::vector<std::string> &args_){
	/*if(cmd_ == "set"){ // Toggle debug mode
		if(args_.size() == 2){
			//Set the module and channel.
			mod_ = atoi(args_.at(0).c_str());
			chan_ = atoi(args_.at(1).c_str());

			//Store previous run status.
			bool runStatus = acqRun_;
			//Stop the run to force the buffer to be cleared.
			acqRun_ = false;
			//Wait until the event buffer has been cleared.
			while(!events_.empty()) usleep(SLEEP_WAIT);
			//Resotre the previous run status.
			acqRun_ = runStatus;
	
			resetGraph_ = true;
		}
		else{
			std::cout << message_head << "Invalid number of parameters to 'set'\n";
			std::cout << message_head << " -SYNTAX- set [module] [channel]\n";
		}
	}
	else if(cmd_ == "single") {
		singleCapture_ = !singleCapture_;
	}
	else if (cmd_ == "thresh") {
		if (args_.size() == 1) {
			threshLow_ = atoi(args_.at(0).c_str());
			threshHigh_ = -1;
		}
		else if (args_.size() == 2) {
			threshLow_ = atoi(args_.at(0).c_str());
			threshHigh_ = atoi(args_.at(1).c_str());
		}
		else {
			std::cout << message_head << "Invalid number of parameters to 'thresh'\n";
			std::cout << message_head << " -SYNTAX- thresh <lowerThresh> [upperThresh]\n";
		}
	}
	else if(cmd_ == "save") {
		if (args_.size() == 1) {
			saveFile_ = args_.at(0);
		}
		else {
			std::cout << message_head << "Invalid number of parameters to 'save'\n";
			std::cout << message_head << " -SYNTAX- save <fileName>\n";
		}
	}
	else if(cmd_ == "delay"){
		if(args_.size() == 1){ delay_ = atoi(args_.at(0).c_str()); }
		else{
			std::cout << message_head << "Invalid number of parameters to 'delay'\n";
			std::cout << message_head << " -SYNTAX- delay [time]\n";
		}
	}
	else if(cmd_ == "log"){
		if(do_drawing_){
			if(canvas->GetLogy()){ 
				canvas->SetLogy(0);
				std::cout << message_head << "y-axis set to linear.\n"; 
			}
			else{ 
				canvas->SetLogy(1); 
				std::cout << message_head << "y-axis set to log.\n"; 
			}
		}
		else{ std::cout << message_head << "Not in drawing mode!\n"; }
	}
	else{ return false; }*/

	return true;
}

/// Scan has stopped data acquisition.
void Skeleton::StopAcquisition(){
	acqRun_ = false;
}

/// Scan has started data acquisition.
void Skeleton::StartAcquisition(){
	acqRun_ = true;	
}

void Skeleton::IdleTask() {
	if(do_drawing_){
		gSystem->ProcessEvents();
	}
}

int main(int argc, char *argv[]){
	// Initialize a new Unpacker object.
	ScanMain scan_main((Unpacker*)(new Skeleton()));
	
	// Force batch mode. Needed if we're going
	// to call execute multiple times.
	scan_main.SetBatchMode();
	
	// Initialize the scanner.
	scan_main.Initialize(argc, argv);
	
	// Set the system message header.
	scan_main.SetMessageHeader("Skeleton: ");

	// Scan the file a given number of times.
	for(int i = 0; i < 1; i++){
		scan_main.Execute();
	}

	return 0;
}
