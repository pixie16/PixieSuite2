/** \rile ScanMain.cpp
 * \brief A class to handle reading from various UTK/ORNL pixie16 data formats.
 *
 * This class is intended to be used as a replacement to the older and unsupported
 * 'scanor' program from the UPAK acq library specifically for .ldf files which are
 * constructed using the UTK/ORNL pixie16 style. This class also interfaces with poll2
 * shared memory output without the need to use pacman.
 * CRT
 *
 * \author C. R. Thornsberry
 * \date Feb. 12th, 2016
 */
#include <iostream>
#include <sstream>
#include <thread>

#include <string.h>
#include <unistd.h>

#include "Unpacker.hpp"
#include "poll2_socket.h"
#include "CTerminal.h"

#include "ScanMain.hpp"

#ifndef PROG_NAME
#define PROG_NAME "ScanMain"
#endif

void start_run_control(ScanMain *main_){
	main_->RunControl();
}

void start_cmd_control(ScanMain *main_){
	main_->CmdControl();
}

/////////////////////////////////////////////////////////////////////
// class fileInformation
/////////////////////////////////////////////////////////////////////

bool fileInformation::at(const size_t &index_, std::string &name, std::string &value){
	if(index_ >= parnames.size()){ return false; }
	name = parnames.at(index_);
	value = parvalues.at(index_);
	return true;
}

template <typename T>
bool fileInformation::push_back(const std::string & name_, const T &value_, const std::string &units_/*=""*/){
	if(!is_in(name_)){
		std::stringstream stream;
		stream << value_;
		if(units_.size() > 0){ 
			stream << " " << units_; 
		}
		parnames.push_back(name_);
		parvalues.push_back(stream.str());
		return true;
	}
	return false;
}

bool fileInformation::is_in(const std::string & name_){
	for(std::vector<std::string>::iterator iter = parnames.begin(); iter != parnames.end(); iter++){
		if(name_ == (*iter)){ return true; }
	}
	return false;
}

std::string fileInformation::print(const size_t &index_){
	if(index_ >= parnames.size()){ return ""; }
	return std::string(parnames.at(index_) + ": " + parvalues.at(index_));
}

/////////////////////////////////////////////////////////////////////
// class ScanMain
/////////////////////////////////////////////////////////////////////

unsigned int ScanMain::split_str(std::string str_, std::vector<std::string> &args, char delimiter_/*=' '*/){
	args.clear();
	std::string temp = "";
	unsigned int count = 0;
	for(unsigned int i = 0; i < str_.size(); i++){
		if(str_[i] == delimiter_ || i == str_.size()-1){
			if(i == str_.size()-1){ temp += str_[i]; }
			args.push_back(temp);
			temp = "";
			count++;
		}
		else{ temp += str_[i]; }		
	}
	return count;
}

std::string ScanMain::get_extension(std::string filename_, std::string &prefix){
	size_t count = 0;
	size_t last_index = 0;
	std::string output = "";
	prefix = "";
	
	if(filename_.find('.') != std::string::npos){
		// Find the final period in the filename
		for(count = 0; count < filename_.size(); count++){
			if(filename_[count] == '.'){ last_index = count; }
		}
	
		// Get the filename prefix and the extension
		for(size_t i = 0; i < count; i++){
			if(i < last_index){ prefix += filename_[i]; }
			else if(i > last_index){ output += filename_[i]; }
		}
	}
	else{ // The filename has no extension.
		prefix = filename_;
	}
	
	return output;
}

void ScanMain::start_scan(){
	if(!init){ std::cout << " Not initialized!\n"; }
	else if(is_running){ std::cout << " Already running.\n"; }
	else{
		is_running = true;
		total_stopped = false;
		core->StartAcquisition();
		if(!batch_mode){ term->SetStatus("\033[0;33m[IDLE]\033[0m Waiting for Unpacker..."); }
	}
}

void ScanMain::stop_scan(){
	if(!init){ std::cout << " Not initialized!\n"; }
	else if(!is_running){ std::cout << " Not running.\n"; }
	else{
		is_running = false;
		core->StopAcquisition();
		if(!batch_mode){ term->SetStatus("\033[0;31m[STOP]\033[0m Acquisition stopped."); }
	}
}

ScanMain::ScanMain(Unpacker *core_/*=NULL*/){
	prefix = "";
	extension = "";

	maxShmSizeL = 4052;
	maxShmSize  = maxShmSizeL * 4;

	max_spill_size = 0;
	file_format = -1;

	file_start_offset = 0;
	num_spills_recvd = 0;
	
	total_stopped = false;
	write_counts = false;
	is_running = false;
	is_verbose = true;
	debug_mode = false;
	dry_run_mode = false;
	shm_mode = false;
	batch_mode = false;
	init = false;

	kill_all = false;
	run_ctrl_exit = false;

	poll_server = NULL;
	term = NULL;
	
	core = core_;
	
	// If a pointer to an Unpacker derived class is not specified, call the
	// extern function GetCore() to get a pointer to a new object.
	if(!core){ core = GetCore(); }

	sys_message_head = std::string(PROG_NAME) + ": ";
}

ScanMain::~ScanMain(){
	Close();
}

void ScanMain::RunControl(){
	// Notify that we are starting run control.
	run_ctrl_exit = false;
	start_scan();

	// Set debug mode, if enabled.
	if(debug_mode){
		pldHead.SetDebugMode();
		pldData.SetDebugMode();
		dirbuff.SetDebugMode();
		headbuff.SetDebugMode();
		databuff.SetDebugMode();
		eofbuff.SetDebugMode();
	}

	while(true){
		if(kill_all){ break; }
	
		// Now we're ready to read the first data buffer
		if(total_stopped){
			// Sleep while waiting for the user to scan more data.
			sleep(1);
			continue;
		}
		else if(shm_mode){
			std::cout << std::endl;
			unsigned int data[250000]; // Array for storing spill data. Larger than any RevF spill should be.
			unsigned int *shm_data = new unsigned int[maxShmSizeL]; // Array to store the temporary shm data (~16 kB)
			int dummy;
			int previous_chunk;
			int current_chunk;
			int total_chunks;
			int nWords;
			unsigned int nTotalWords;
	
			bool full_spill = false;
		
			while(true){
				if(kill_all == true){ 
					break;
				}
				else if(!is_running){
					sleep(1);
					continue;
				}

				int select_dummy;
				previous_chunk = 0;
				current_chunk = 0;
				total_chunks = -1;
				nTotalWords = 0;
				full_spill = true;

				if(!poll_server->Select(dummy)){
					if(!batch_mode){ term->SetStatus("\033[0;33m[IDLE]\033[0m Waiting for a spill..."); }
					else{ std::cout << "\r\033[0;33m[IDLE]\033[0m Waiting for a spill..."; }
					core->IdleTask();
					continue; 
				}
		
				if(!poll_server->Select(select_dummy)){ continue; } // Server timeout
		
				// Get the spill
				while(current_chunk != total_chunks){
					if(!poll_server->Select(select_dummy)){ // Server timeout
						std::cout << sys_message_head << "Network timeout before recv full spill!\n";
						full_spill = false;
						break;
					} 

					nWords = poll_server->RecvMessage((char*)shm_data, maxShmSize) / 4; // Read from the socket
					if(strcmp((char*)shm_data, "$CLOSE_FILE") == 0 || strcmp((char*)shm_data, "$OPEN_FILE") == 0 || strcmp((char*)shm_data, "$KILL_SOCKET") == 0){ continue; } // Poll2 network flags
					// Did not read enough bytes
					else if(nWords < 2){
						continue;
					}

					if(debug_mode){ std::cout << "debug: Received " << nWords << " words from the network\n"; }
					memcpy((char *)&current_chunk, &shm_data[0], 4);
					memcpy((char *)&total_chunks, &shm_data[1], 4);

					if(previous_chunk == -1 && current_chunk != 1){ // Started reading in the middle of a spill, ignore the rest of it
						if(debug_mode){ std::cout << "debug: Skipping chunk " << current_chunk << " of " << total_chunks << std::endl; }
						continue;
					}
					else if(previous_chunk != current_chunk - 1){ // We missed a spill chunk somewhere
						if(debug_mode){ std::cout << "debug: Found chunk " << current_chunk << " but expected chunk " << previous_chunk+1 << std::endl; }
						break;
					}

					previous_chunk = current_chunk;
		
					// Copy the shm spill chunk into the data array
					if(nTotalWords + 2 + nWords <= 250000){ // This spill chunk will fit into the data buffer
						memcpy(&data[nTotalWords], &shm_data[2], (nWords - 2)*4);
						nTotalWords += (nWords - 2);				
					}
					else{ 
						if(debug_mode){ std::cout << "debug: Abnormally full spill buffer with " << nTotalWords + 2 + nWords << " words!\n"; }
						break; 
					}
				}

				std::stringstream status;
				status << "\033[0;32m" << "[RECV] " << "\033[0m" << nTotalWords << " words";
				if(!batch_mode){ term->SetStatus(status.str()); }
				else{ std::cout << "\r" << status.str(); }
		
				if(debug_mode){ std::cout << "debug: Retrieved spill of " << nTotalWords << " words (" << nTotalWords*4 << " bytes)\n"; }
				if(!dry_run_mode && full_spill){ 
					int word1 = 2, word2 = 9999;
					memcpy(&data[nTotalWords], (char *)&word1, 4);
					memcpy(&data[nTotalWords+1], (char *)&word2, 4);
					core->ReadSpill(data, nTotalWords + 2, is_verbose); 
				}
			
				if(!full_spill){ std::cout << sys_message_head << "Not processing spill fragment!\n"; }
				else{ num_spills_recvd++; }
			}
		
			delete[] shm_data;
		}
		else if(file_format == 0){
			unsigned int *data = NULL;
			bool full_spill;
			bool bad_spill;
			unsigned int nBytes;
		
			if(!dry_run_mode){ data = new unsigned int[250000]; }
		
			while(true){ 
				if(kill_all == true){ 
					break;
				}
				else if(!is_running){
					sleep(1);
					continue;
				}

				if(!databuff.Read(&input_file, (char*)data, nBytes, 1000000, full_spill, bad_spill, dry_run_mode)){
					if(databuff.GetRetval() == 2){
						if(debug_mode){ std::cout << "debug: Encountered double EOF buffer.\n"; }
						break;
					}
					else if(databuff.GetRetval() == 6){
						if(debug_mode){ std::cout << "debug: Failed to read from input file.\n"; }
						break;
					}
					continue;
				}

				std::stringstream status;			
				status << "\033[0;32m" << "[READ] " << "\033[0m" << nBytes/4 << " words (" << 100*input_file.tellg()/file_length << "%)";
				if(!batch_mode){ term->SetStatus(status.str()); }
				else{ std::cout << "\r" << status.str(); }
		
				if(full_spill){ 
					if(debug_mode){ 
						std::cout << "debug: Retrieved spill of " << nBytes << " bytes (" << nBytes/4 << " words)\n"; 
						std::cout << "debug: Read up to word number " << input_file.tellg()/4 << " in input file\n";
					}
					if(!dry_run_mode){ 
						if(!bad_spill){ 
							core->ReadSpill(data, nBytes/4, is_verbose); 
						}
						else{ std::cout << " WARNING: Spill has been flagged as corrupt, skipping (at word " << input_file.tellg()/4 << " in file)!\n"; }
					}
				}
				else if(debug_mode){ 
					std::cout << "debug: Retrieved spill fragment of " << nBytes << " bytes (" << nBytes/4 << " words)\n"; 
					std::cout << "debug: Read up to word number " << input_file.tellg()/4 << " in input file\n";
				}
				num_spills_recvd++;
			}

			if(!dry_run_mode){ delete[] data; }
		
			if(!batch_mode){ term->SetStatus("\033[0;33m[IDLE]\033[0m Finished scanning file."); }
			else{ std::cout << std::endl << std::endl; }
		}
		else if(file_format == 1){
			unsigned int *data = NULL;
			int nBytes;
		
			if(!dry_run_mode){ data = new unsigned int[max_spill_size+2]; }
		
			while(pldData.Read(&input_file, (char*)data, nBytes, 4*max_spill_size, dry_run_mode)){ 
				if(kill_all == true){ 
					break;
				}
				else if(!is_running){
					sleep(1);
					continue;
				}

				std::stringstream status;
				status << "\033[0;32m" << "[READ] " << "\033[0m" << nBytes/4 << " words (" << 100*input_file.tellg()/file_length << "%)";
				if(!batch_mode){ term->SetStatus(status.str()); }
				else{ std::cout << "\r" << status.str(); }
		
				if(debug_mode){ 
					std::cout << "debug: Retrieved spill of " << nBytes << " bytes (" << nBytes/4 << " words)\n"; 
					std::cout << "debug: Read up to word number " << input_file.tellg()/4 << " in input file\n";
				}
			
				if(!dry_run_mode){ 
					int word1 = 2, word2 = 9999;
					memcpy(&data[(nBytes/4)], (char *)&word1, 4);
					memcpy(&data[(nBytes/4)+1], (char *)&word2, 4);
					core->ReadSpill(data, nBytes/4 + 2, is_verbose); 
				}
				num_spills_recvd++;
			}

			if(eofbuff.ReadHeader(&input_file)){
				std::cout << sys_message_head << "Encountered EOF buffer.\n";
			}
			else{
				std::cout << sys_message_head << "Failed to find end of file buffer!\n";
			}
		
			if(!dry_run_mode){ delete[] data; }
		
			if(!batch_mode){ term->SetStatus("\033[0;33m[IDLE]\033[0m Finished scanning file."); }
			else{ std::cout << std::endl << std::endl; }
		}
		else if(file_format == 2){
		}
		
		total_stopped = true;
		stop_scan();
		
		if(batch_mode){ break; }
	}
	
	// Notify that run control is exiting.
	run_ctrl_exit = true;
}

void ScanMain::CmdControl(){
	if(!core){ return; }

	std::string cmd = "", arg;

	while(true){
		if(run_ctrl_exit){ break; }
	
		cmd = term->GetCommand();
		if(cmd == "_SIGSEGV_"){
			std::cout << "\033[0;31m[SEGMENTATION FAULT]\033[0m\n";
			exit(EXIT_FAILURE);
		}
		else if(cmd == "CTRL_D"){ 
			std::cout << sys_message_head << "Received EOF (ctrl-d) signal. Exiting...\n";
			cmd = "quit"; 
		}
		else if(cmd == "CTRL_C"){ 
			std::cout << sys_message_head << "Warning! Received SIGINT (ctrl-c) signal.\n";
			continue; 
		}
		else if(cmd == "CTRL_Z"){ 
			std::cout << sys_message_head << "Warning! Received SIGTSTP (ctrl-z) signal.\n";
			continue; 
		}
		term->flush();

		if(cmd == ""){ continue; }
		
		size_t index = cmd.find(" ");
		if(index != std::string::npos){
			arg = cmd.substr(index+1, cmd.size()-index); // Get the argument from the full input string
			cmd = cmd.substr(0, index); // Get the command from the full input string
		}
		else{ arg = ""; }

		std::vector<std::string> arguments;
		split_str(arg, arguments);
		
		if(cmd == "quit" || cmd == "exit"){
			kill_all = true;
			core->KillAll();
			while(!run_ctrl_exit){ sleep(1); }
			break;
		}
		else if(cmd == "version" || cmd == "v"){
			std::cout << "  " << PROG_NAME << "      v" << SCAN_VERSION << " (" << SCAN_DATE << ")\n";
			std::cout << "  Poll2 Socket  v" << POLL2_SOCKET_VERSION << " (" << POLL2_SOCKET_DATE << ")\n"; 
			std::cout << "  HRIBF Buffers v" << HRIBF_BUFFERS_VERSION << " (" << HRIBF_BUFFERS_DATE << ")\n"; 
			std::cout << "  CTerminal     v" << CTERMINAL_VERSION << " (" << CTERMINAL_DATE << ")\n";
		}
		else if(cmd == "help" || cmd == "h"){
			std::cout << "  Help:\n";
			std::cout << "   debug       - Toggle debug mode flag (default=false)\n";
			std::cout << "   quiet       - Toggle quiet mode flag (default=false)\n";
			std::cout << "   quit        - Close the program\n";
			std::cout << "   help (h)    - Display this dialogue\n";
			std::cout << "   version (v) - Display Poll2 version information\n";
			std::cout << "   run         - Start acquisition\n";
			std::cout << "   stop        - Stop acquisition\n";
			std::cout << "   rewind      - Rewind to the beginning of the file\n";
			core->CmdHelp("   ");
		}
		else if(cmd == "run"){ // Start acquisition.
			start_scan();
		}
		else if(cmd == "stop"){ // Stop acquisition.
			stop_scan();
		}
		else if(cmd == "debug"){ // Toggle debug mode
			if(debug_mode){
				std::cout << sys_message_head << "Toggling debug mode OFF\n";
				debug_mode = false;
			}
			else{
				std::cout << sys_message_head << "Toggling debug mode ON\n";
				debug_mode = true;
			}
			core->SetDebugMode(debug_mode);
		}
		else if(cmd == "quiet"){ // Toggle quiet mode
			if(!is_verbose){
				std::cout << sys_message_head << "Toggling quiet mode OFF\n";
				is_verbose = true;
			}
			else{
				std::cout << sys_message_head << "Toggling quiet mode ON\n";
				is_verbose = false;
			}
		}
		else if(cmd == "rewind"){ // Rewind the file to the start position
			Rewind();
		}
		else if(!core->CommandControl(cmd, arguments)){ // Unrecognized command. Send it to Unpacker.
			std::cout << sys_message_head << "Unknown command '" << cmd << "'\n";
		}
	}		
}

void ScanMain::Help(char *name_, Unpacker *core){
	core->SyntaxStr(name_, " ");
	std::cout << "  Available options:\n";
	std::cout << "   --help (-h)    - Display this dialogue\n";
	std::cout << "   --version (-v) - Display version information\n";
	std::cout << "   --debug        - Enable readout debug mode\n";
	std::cout << "   --shm          - Enable shared memory readout\n";
	std::cout << "   --ldf          - Force use of ldf readout\n";
	std::cout << "   --pld          - Force use of pld readout\n";
	std::cout << "   --root         - Force use of root readout\n";
	std::cout << "   --quiet        - Toggle off verbosity flag\n";
	std::cout << "   --counts       - Write all recorded channel counts to a file\n";
	std::cout << "   --batch        - Run in batch mode (i.e. with no command line)\n";
	std::cout << "   --dry-run      - Extract spills from file, but do no processing\n";
	std::cout << "   --fast-fwd [word] - Skip ahead to a specified word in the file (start of file at zero)\n";
	core->ArgHelp("   ");
}

bool ScanMain::Initialize(int argc, char *argv[]){
	if(init){ return false; }

	if(argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)){ // A stupid way to do this... for now.
		Unpacker *temp_core = GetCore();
		Help(argv[0], temp_core);
		delete temp_core;
		return false;
	}
	else if(argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)){ // Display version information
		std::cout << "  " << PROG_NAME << "      v" << SCAN_VERSION << " (" << SCAN_DATE << ")\n";
		std::cout << "  Poll2 Socket  v" << POLL2_SOCKET_VERSION << " (" << POLL2_SOCKET_DATE << ")\n"; 
		std::cout << "  HRIBF Buffers v" << HRIBF_BUFFERS_VERSION << " (" << HRIBF_BUFFERS_DATE << ")\n"; 
		std::cout << "  CTerminal     v" << CTERMINAL_VERSION << " (" << CTERMINAL_DATE << ")\n";
		return false;
	}
	
	debug_mode = false;
	dry_run_mode = false;
	shm_mode = false;
	
	num_spills_recvd = 0;

	// Fill the argument list.
	std::deque<std::string> scan_args;
	std::deque<std::string> coreargs;
	int arg_index = 1;
	while(arg_index < argc){
		scan_args.push_back(std::string(argv[arg_index++]));
	}

	core->SetMsgPrefix(sys_message_head);
	
	// Loop through the arg list and extract ScanMain arguments.
	std::string current_arg;
	while(!scan_args.empty()){
		current_arg = scan_args.front();
		scan_args.pop_front();
	
		if(current_arg == "--debug"){ 
			core->SetDebugMode();
			debug_mode = true;
		}
		else if(current_arg == "--counts"){
			write_counts = true;
		}
		else if(current_arg == "--batch"){
			batch_mode = true;
		}
		else if(current_arg == "--dry-run"){
			dry_run_mode = true;
		}
		else if(current_arg == "--fast-fwd"){
			if(scan_args.empty()){
				std::cout << " FATAL ERROR! Missing required argument to option '--fast-fwd'!\n";
				Help(argv[0], core);
				return false;
			}
			file_start_offset = atoll(scan_args.front().c_str());
			scan_args.pop_front();
		}
		else if(current_arg == "--quiet"){
			is_verbose = false;
		}
		else if(current_arg == "--shm"){ 
			file_format = 0;
			shm_mode = true;
			core->SetSharedMemMode(true);
			std::cout << " Using shm mode!\n";
		}
		else if(current_arg == "--ldf"){ 
			file_format = 0;
		}
		else if(current_arg == "--pld"){ 
			file_format = 1;
		}
		else if(current_arg == "--root"){ 
			file_format = 2;
		}
		else{ coreargs.push_back(current_arg); } // Unrecognized option.
	}

	std::string input_filename = "";
	if(!core->SetArgs(coreargs, input_filename)){ 
		Help(argv[0], core);
		return false; 
	}

	if(!shm_mode){
		extension = get_extension(input_filename, prefix);
		if(file_format != -1){
			if(file_format == 0){ std::cout << sys_message_head << "Forcing ldf file readout.\n"; }
			else if(file_format == 1){ std::cout << sys_message_head << "Forcing pld file readout.\n"; }
			else if(file_format == 2){ std::cout << sys_message_head << "Forcing root file readout.\n"; }
		}
		else{
			if(prefix == ""){
				std::cout << " FATAL ERROR! Input filename was not specified!\n";
				return false;
			}
		
			if(extension == "ldf"){ // List data format file
				file_format = 0;
			}
			else if(extension == "pld"){ // Pixie list data file format
				file_format = 1;
			}
			else if(extension == "root"){ // Pixie list data file format
				file_format = 2;
			}
			else{
				std::cout << " FATAL ERROR! Invalid file format '" << extension << "'\n";
				std::cout << "  The current valid data formats are:\n";
				std::cout << "   ldf - list data format (HRIBF)\n";
				std::cout << "   pld - pixie list data format\n";
				std::cout << "   root - root file containing raw pixie data\n";
				return false;
			}
		}
	}

	// Initialize the Unpacker object.
	std::cout << sys_message_head << "Initializing data unpacker object.\n";
	if(!core->Initialize(sys_message_head)){ // Failed to initialize the unpacker object. Clean up and exit.
		std::cout << " FATAL ERROR! Failed to initialize unpacker object!\n";
		std::cout << "\nCleaning up...\n";
		core->PrintStatus(sys_message_head);
		core->Close(write_counts);
		return false;
	}

	if(!shm_mode){
		std::cout << sys_message_head << "Using file prefix " << prefix << ".\n";
		input_file.open((prefix+"."+extension).c_str(), std::ios::binary);
		if(!input_file.is_open() || !input_file.good()){
			std::cout << " FATAL ERROR! Failed to open input file '" << prefix+"."+extension << "'! Check that the path is correct.\n";
			input_file.close();
			return 1;
		}
		input_file.seekg(0, input_file.end);
	 	file_length = input_file.tellg();
	 	input_file.seekg(0, input_file.beg);
	}
	else{
		poll_server = new Server();
		if(!poll_server->Init(5555, 1)){
			std::cout << " FATAL ERROR! Failed to open shm socket 5555!\n";
			return 1;
		}
	}

	if(shm_mode && batch_mode){
		std::cout << sys_message_head << "Unable to enable batch mode for shared-memory mode!\n";
		batch_mode = false;
	}

	// Initialize the terminal.
	std::string temp_name = std::string(PROG_NAME);
	
	if(!batch_mode){
		term = new Terminal();
		term->Initialize();
		term->SetCommandHistory(("."+temp_name+".cmd").c_str());
		term->SetPrompt((temp_name+" $ ").c_str());
		term->AddStatusWindow();
	}

	std::cout << "\n " << PROG_NAME << " v" << SCAN_VERSION << "\n"; 
	std::cout << " ==  ==  ==  ==  == \n\n"; 

	if(debug_mode){ std::cout << sys_message_head << "Using debug mode.\n\n"; }
	if(dry_run_mode){ std::cout << sys_message_head << "Doing a dry run.\n\n"; }
	if(shm_mode){ 
		std::cout << sys_message_head << "Using shared-memory mode.\n\n"; 
		std::cout << sys_message_head << "Listening on poll2 SHM port 5555\n\n";
	}

	if(!shm_mode){
		// Start reading the file
		// Every poll2 ldf file starts with a DIR buffer followed by a HEAD buffer
		int num_buffers;
		if(file_format == 0){
			dirbuff.Read(&input_file, num_buffers);
			headbuff.Read(&input_file);
			
			// Store the file information for later use.
			finfo.push_back("Run number", dirbuff.GetRunNumber());
			finfo.push_back("Number buffers", num_buffers);
			finfo.push_back("Facility", headbuff.GetFacility());
			finfo.push_back("Format", headbuff.GetFormat());
			finfo.push_back("Type", headbuff.GetType());
			finfo.push_back("Date", headbuff.GetDate());
			finfo.push_back("Title", headbuff.GetRunTitle());
			
			// Let's read out the file information from these buffers
			std::cout << " 'DIR ' buffer-\n";
			std::cout << "  " << finfo.print(0) << "\n";
			std::cout << "  " << finfo.print(1) << "\n";
			std::cout << " 'HEAD' buffer-\n";
			std::cout << "  " << finfo.print(2) << "\n";
			std::cout << "  " << finfo.print(3) << "\n";
			std::cout << "  " << finfo.print(4) << "\n";
			std::cout << "  " << finfo.print(5) << "\n";
			std::cout << "  " << finfo.print(6) << "\n";
			std::cout << "  Run number: " << headbuff.GetRunNumber() << "\n\n";
		}
		else if(file_format == 1){
			pldHead.Read(&input_file);
			
			max_spill_size = pldHead.GetMaxSpillSize();

			// Store the file information for later use.
			finfo.push_back("Facility", pldHead.GetFacility());
			finfo.push_back("Format", pldHead.GetFormat());
			finfo.push_back("Start", pldHead.GetStartDate());
			finfo.push_back("Stop", pldHead.GetEndDate());
			finfo.push_back("Title", pldHead.GetRunTitle());
			finfo.push_back("Run number", pldHead.GetRunNumber());
			finfo.push_back("Max spill", max_spill_size, "words");
			finfo.push_back("ACQ time", pldHead.GetRunTime(), "seconds");
			
			// Let's read out the file information from this buffer
			std::cout << " 'HEAD' buffer-\n";
			std::cout << "  " << finfo.print(0) << "\n";
			std::cout << "  " << finfo.print(1) << "\n";
			std::cout << "  " << finfo.print(2) << "\n";
			std::cout << "  " << finfo.print(3) << "\n";
			std::cout << "  " << finfo.print(4) << "\n";
			std::cout << "  " << finfo.print(5) << "\n";
			std::cout << "  " << finfo.print(6) << "\n";
			std::cout << "  " << finfo.print(7) << "\n\n";			
		}
		else if(file_format == 2){
		}
	}
	
	// Do any last minute initialization.
	try{ core->FinalInitialization(); }
	catch(...){ std::cout << "\nUnpacker object final initialization failed!\n"; }

	return (init = true);
}

bool ScanMain::Rewind(){
	if(!init){ return false; }

	// Ensure that the scan is not running.
	if(is_running){ 
		std::cout << " Cannot change file position while scan is running!\n";
		return false;
	}

	// Move to the first word in the file.
	std::cout << " Seeking to word no. " << file_start_offset << " in file\n";
	input_file.seekg(file_start_offset*4, input_file.beg);
	std::cout << " Input file is now at " << input_file.tellg() << " bytes\n";

	return true;
}

int ScanMain::Execute(){
	if(!init){
		std::cout << " FATAL ERROR! ScanMain is not initialized!\n";
		return 1; 
	}

	// Seek to the beginning of the file.
	if(file_start_offset != 0){ Rewind(); }

	// Process the file.
	if(!batch_mode){
		// Start the run control thread
		std::cout << "\n Starting data control thread\n";
		std::thread runctrl(start_run_control, this);

		// Start the command control thread. This needs to be the last thing we do to
		// initialize, so the user cannot enter commands before setup is complete
		std::cout << " Starting command thread\n\n";
		std::thread comctrl(start_cmd_control, this);

		// Synchronize the threads and wait for completion
		comctrl.join();
		runctrl.join();
	}
	else{ start_run_control(this); }
	
	return 0;
}

bool ScanMain::Close(){
	if(!init){ return false; }

	// Close the socket and restore the terminal
	if(!batch_mode){
		term->Close();
	}
	
	// Only close the server if this is shared memory mode. Otherwise
	// the server would never have been initialized.
	if(shm_mode){ poll_server->Close(); }
	
	//Reprint the leader as the carriage was returned
	std::cout << "Running " << PROG_NAME << " v" << SCAN_VERSION << " (" << SCAN_DATE << ")\n";
	
	std::cout << sys_message_head << "Retrieved " << num_spills_recvd << " spills!\n";

	input_file.close();	

	// Clean up detector driver
	std::cout << "\nCleaning up...\n";
	
	core->PrintStatus(sys_message_head);
	core->Close(write_counts);
	
	if(poll_server){ delete poll_server; }
	if(term){ delete term; }
	if(core){ delete core; }
	
	return !(init = false);
}
