// poll2.cpp
// Cory R. Thornsberry
// Jan. 21st, 2015
//
// This program is designed as a replacement of the POLL program for reading
// VANDLE data from PIXIE-16 crates. The old POLL program relied on a connection
// to PACMAN in order to recieve commands whereas this is a stand-alone program
// with a built-in command line interface. The .ldf file format is retained, but
// this program does not split buffers to fit into the HRIBF standard buffer.
// Data is also not transmitted onto a socket (except for shm). Instead, .ldf
// files are generated by this program directly.

#include <iostream>
#include <thread>
#include <utility>
#include <map>

#include "poll2_core.h"
#include "Display.h"
#include "StatsHandler.hpp"
#include "CTerminal.h"

/* Print help dialogue for command line options. */
void help(){
	std::cout << "\n SYNTAX: ./poll2 <options>\n";
	std::cout << "  -a, --alarm=[e-mail] Call the alarm script with a given e-mail (or no argument)\n"; 
	std::cout << "  -f, --fast           Fast boot (false by default)\n";
	std::cout << "  -q, --quiet          Run quietly (false by default)\n";
	std::cout << "  -n, --no-wall-clock  Do not insert the wall clock in the data stream\n";
	std::cout << "  -r, --rates          Display module rates in quiet mode (false by defualt)\n";
	std::cout << "  -t, --thresh <num>   Sets FIFO read threshold to num% full (50% by default)\n";
	std::cout << "  -z, --zero           Zero clocks on each START_ACQ (false by default)\n";
	std::cout << "  -d, --debug          Set debug mode to true (false by default)\n";
	std::cout << "  -p, --pacman         Use classic poll operation for use with Pacman.\n";
	std::cout << "  -h, --help           Display this help dialogue.\n\n";
}	
	
void start_run_control(Poll *poll_){
	poll_->RunControl();
}

void start_cmd_control(Poll *poll_){
	poll_->CommandControl();
}

int main(int argc, char *argv[]){
	// Read the FIFO when it is this full
	unsigned int threshPercent = 50;
	std::string alarmArgument = "";

	// Define all valid command line options
	// This is done to keep legacy options available while removing dependency on HRIBF libraries
	CLoption valid_opt[11];
	valid_opt[0].Set("alarm", false, true);
	valid_opt[1].Set("fast", false, false);
	valid_opt[2].Set("quiet", false, false);
	valid_opt[3].Set("no-wall-clock", false, false);
	valid_opt[4].Set("rates", false, false);
	valid_opt[5].Set("thresh", true, false);
	valid_opt[6].Set("zero", false, false);
	valid_opt[7].Set("debug", false, false);
	valid_opt[8].Set("pacman", false, false);
	valid_opt[9].Set("help", false, false);
	valid_opt[10].Set("?", false, false);
	if(!get_opt(argc, argv, valid_opt, 11, help)){ return 1; }

	// Help
	if(valid_opt[9].is_active){
		help();
		return 0;
	}	

	Terminal poll_term;

	//We make sure the system isn't locked first.
	// This avoids issues with curses.
	Lock *lock = new Lock("PixieInterface");
	delete lock;

	// Main object
	Poll poll;
	
	// Set all of the selected options
	if(valid_opt[0].is_active){
		if(valid_opt[0].value != ""){ alarmArgument = valid_opt[0].value; }
		poll.SetSendAlarm();
	}
	if(valid_opt[1].is_active){ poll.SetBootFast(); }
	if(valid_opt[2].is_active){ poll.SetQuietMode(); }
	if(valid_opt[3].is_active){ poll.SetWallClock(); }
	if(valid_opt[4].is_active){ poll.SetShowRates(); }
	if(valid_opt[5].is_active){ 
		threshPercent = atoi(valid_opt[5].value.c_str()); 
		if(threshPercent <= 0){ 
			std::cout << Display::WarningStr("Warning!") << " failed to set threshold level. Using default of 50%\n";
			threshPercent = 50; 
		}
	}
	if(valid_opt[6].is_active){ poll.SetZeroClocks(); }
	if(valid_opt[7].is_active){ poll.SetDebugMode(); }
	if(valid_opt[8].is_active){ poll.SetPacmanMode(); }
	if(valid_opt[10].is_active){ return 0; }

	if(!poll.Initialize()){ return 1; }

	// Initialize the terminal before doing anything else;
	poll_term.Initialize(".poll2.cmd");
	poll_term.SetPrompt(Display::InfoStr("POLL2 $ ").c_str());
	poll_term.AddStatusWindow();
	poll_term.EnableTabComplete();
	poll_term.SetLogFile(".poll2.log");
	if (poll.GetPacmanMode()) poll_term.EnableTimeout();

	std::cout << "\n#########      #####    ####      ####       ########\n"; 
	std::cout << " ##     ##    ##   ##    ##        ##       ##      ##\n";
	std::cout << " ##      ##  ##     ##   ##        ##                ##\n";
	std::cout << " ##     ##  ##       ##  ##        ##               ##\n";
	std::cout << " #######    ##       ##  ##        ##              ##\n";
	std::cout << " ##         ##       ##  ##        ##            ##\n";
	std::cout << " ##         ##       ##  ##        ##           ##\n";
	std::cout << " ##          ##     ##   ##        ##         ##\n";
	std::cout << " ##           ##   ##    ##    ##  ##    ##  ##\n";
	std::cout << "####           #####    ######### ######### ###########\n";

	std::cout << "\n POLL2 v" << POLL2_CORE_VERSION << "\n"; 
	std::cout << " ==  ==  ==  ==  == \n\n"; 
	
	poll.SetThreshWords(EXTERNAL_FIFO_LENGTH * threshPercent / 100.0);
	std::cout << "Using FIFO threshold of " << poll.GetThreshWords() << " words\n";
	
#ifdef PIF_REVA
	std::cout << "Using Pixie16 revision A\n";
#elif (defined PIF_REVD)
	std::cout << "Using Pixie16 revision D\n";
#elif (defined PIF_REVF)
	std::cout << "Using Pixie16 revision F\n";
#else
	std::cout << "Using unknown Pixie16 revision!!!\n";
#endif

	if(poll.GetPacmanMode()){ std::cout << "Using pacman mode!\n"; }
	std::cout << std::endl;

  	StatsHandler handler(poll.GetNcards());
  	poll.SetStatsHandler(&handler);
	poll.SetTerminal(&poll_term);
  	
	if(poll.GetSendAlarm()){
		Display::LeaderPrint("Sending alarms to");
		if(alarmArgument.empty()){ std::cout << Display::InfoStr("DEFAULT") << std::endl; }
		else { std::cout << Display::WarningStr(alarmArgument) << std::endl; }
	}

	// Start the run control thread
	std::cout << pad_string("Starting run control thread", 49);
	std::thread runctrl(start_run_control, &poll);
	std::cout << Display::OkayStr() << std::endl;

	// Start the command control thread. This needs to be the last thing we do to
	// initialize, so the user cannot enter commands before setup is complete
	std::cout << pad_string("Starting command thread", 49);
	std::thread comctrl(start_cmd_control, &poll);
	std::cout << Display::OkayStr() << std::endl << std::endl;
	
	// Synchronize the threads and wait for completion
	comctrl.join();
	runctrl.join();

	// Close the output file, if one is open
	poll.Close();

	//Reprint the leader as the carriage was returned
	Display::LeaderPrint(std::string("Running poll2 v").append(POLL2_CORE_VERSION));
	std::cout << Display::OkayStr("[Done]") << std::endl;

	return 0;
}
