/** \file CTerminal.cpp
  * 
  * \brief Library to handle all aspects of a stand-alone command line interface
  *
  * Library to facilitate the creation of C++ executables with
  * interactive command line interfaces under a linux environment
  *
  * \author Cory R. Thornsberry
  * 
  * \date May 13th, 2015
  * 
  * \version 1.1.06
*/

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <vector>

#ifdef USE_NCURSES

#include <signal.h>
#include <stdexcept>

#endif

#include "CTerminal.h"

#include "TermColors.h"

#ifdef USE_NCURSES

bool SIGNAL_INTERRUPT = false;
bool SIGNAL_RESIZE = false;

#endif

template <typename T>
std::string to_str(const T &input_){
	std::stringstream stream;
	stream << input_;
	return stream.str();
}

// Default target for get_opt
void dummy_help(){}

///////////////////////////////////////////////////////////////////////////////
// CLoption
///////////////////////////////////////////////////////////////////////////////

/// Parse all command line entries and find valid options.
bool get_opt(unsigned int argc_, char **argv_, CLoption *options, unsigned int num_valid_opt_, void (*help_)()/*=dummy_help*/){
	unsigned int index = 1;
	unsigned int previous_opt= 0;
	bool need_an_argument = false;
	bool may_have_argument = false;
	bool is_valid_argument = false;
	while(index < argc_){
		if(argv_[index][0] == '-'){
			if(need_an_argument){
				std::cout << "\n Error: --" << options[previous_opt].alias << " [-" << options[previous_opt].opt << "] requires an argument\n";
				help_();
				return false;
			}

			is_valid_argument = false;
			if(argv_[index][1] == '-'){ // Word options
				std::string word_arg = csubstr(argv_[index], 2);
				for(unsigned int i = 0; i < num_valid_opt_; i++){
					if(word_arg == options[i].alias && word_arg != "NULL"){
						options[i].is_active = true; 
						previous_opt = i;
						if(options[i].require_arg){ need_an_argument = true; }
						else{ need_an_argument = false; }
						if(options[i].optional_arg){ may_have_argument = true; }
						else{ may_have_argument = false; }
						is_valid_argument = true;
					}
				}
				if(!is_valid_argument){
					std::cout << "\n Error: encountered unknown option --" << word_arg << std::endl;
					help_();
					return false;
				}
			}
			else{ // Character options
				unsigned int index2 = 1;
				while(argv_[index][index2] != '\0'){
					for(unsigned int i = 0; i < num_valid_opt_; i++){
						if(argv_[index][index2] == options[i].opt && argv_[index][index2] != 0x0){ 
							options[i].is_active = true; 
							previous_opt = i;
							if(options[i].require_arg){ need_an_argument = true; }
							else{ need_an_argument = false; }
							if(options[i].optional_arg){ may_have_argument = true; }
							else{ may_have_argument = false; }
							is_valid_argument = true;
						}
					}
					if(!is_valid_argument){
						std::cout << "\n Error: encountered unknown option -" << argv_[index][index2] << std::endl;
						help_();
						return false;
					}
					index2++;
				}
			}
		}
		else{ // An option argument
			if(need_an_argument || may_have_argument){
				options[previous_opt].value = csubstr(argv_[index]);
				need_an_argument = false;
				may_have_argument = false;
			}
			else{
                            std::cout << "\n Error: --" << options[previous_opt].alias << " [-" << options[previous_opt].opt << "] takes no argument\n";
				help_();
				return false;			
			}
		}
		
		// Check for the case where the end option requires an argument, but did not receive it
		if(index == argc_-1 && need_an_argument){
			std::cout << "\n Error: --" << options[previous_opt].alias << " [-" << options[previous_opt].opt << "] requires an argument\n";
			help_();
			return false;	
		}
		
		index++;
	}
	return true;
}

/// Return the length of a character string.
unsigned int cstrlen(const char *str_){
	unsigned int output = 0;
	while(true){
		if(str_[output] == '\0'){ break; }
		output++;
	}
	return output;
}

/// Extract a string from a character array.
std::string csubstr(char *str_, unsigned int start_index_/*=0*/){
	std::string output = "";
	unsigned int index = start_index_;
	while(str_[index] != '\0' && str_[index] != ' '){
		output += str_[index];
		index++;
	}
	return output;
}

///////////////////////////////////////////////////////////////////////////////
// CommandHolder
///////////////////////////////////////////////////////////////////////////////

/// Push a new command into the storage array
void CommandHolder::Push(std::string &input_){
	input_.erase(input_.find_last_not_of(" \n\t\r")+1);
	commands[index] = input_;
	total++;
	index++;
	external_index = 0;
	
	if(index >= max_size){ index = 0; }
}

/// Clear the command array
void CommandHolder::Clear(){
	for(unsigned int i = 0; i < max_size; i++){
		commands[i] = "";
	}
}

/** Convert the external index (relative to the most recent command) to the internal index
  * which is used to actually access the stored commands in the command array. */
unsigned int CommandHolder::wrap_(){
	if(index < external_index){
		unsigned int temp = (external_index - index) % max_size;
		return max_size - temp;
	}
	else if(index >= max_size + external_index){
		unsigned int temp = (index - external_index) % max_size;
		return temp;
	}
	return (index - external_index);
}

/// Get the previous command entry
std::string CommandHolder::GetPrev(){
	if(total == 0){ return "NULL"; }

	external_index++;
	if(external_index >= max_size){ 
		external_index = max_size - 1; 
	}
	else if(external_index >= total){
		external_index = total;
	}

	return commands[wrap_()];
}

/// Get the previous command entry but do not change the internal array index
std::string CommandHolder::PeekPrev(){
	if(total == 0){ return "NULL"; }
	
	if(index == 0){ return commands[max_size-1]; }
	return commands[index-1];
}

/// Get the next command entry
std::string CommandHolder::GetNext(){
	if(total == 0){ return "NULL"; }

	if(external_index >= 1){
		external_index--;
	}

	if(external_index == 0){ return fragment; }
	return commands[wrap_()]; 
}

/// Get the next command entry but do not change the internal array index
std::string CommandHolder::PeekNext(){
	if(total == 0){ return "NULL"; }
	
	if(index == max_size-1){ return commands[0]; }
	return commands[index+1];
}

/// Dump all stored commands to the screen
void CommandHolder::Dump(){
	for(unsigned int i = 0; i < max_size; i++){
		if(commands[i] != ""){ std::cout << " " << i << ": " << commands[i] << std::endl; }
	}
}

void CommandHolder::Reset() {
	external_index = 0;	
}

///////////////////////////////////////////////////////////////////////////////
// CommandString
///////////////////////////////////////////////////////////////////////////////

/// Put a character into string at specified position.
void CommandString::Put(const char ch_, unsigned int index_){
	if(index_ < 0){ return; }
	else if(index_ < command.size()){ // Overwrite or insert a character
		if(!insert_mode) { command.insert(index_, 1, ch_); } // Insert
		else { command.at(index_) = ch_; } // Overwrite
	}
	else{ command.push_back(ch_); } // Appending to the back of the string
}

void CommandString::Insert(size_t pos, const char* str) {
	command.insert(pos,str);
}

/// Remove a character from the string.
void CommandString::Pop(unsigned int index_){
	if(index_ < 0){ return ; }
	else if(index_ < command.size()){ // Pop a character out of the string
		command.erase(index_, 1);
	}
}

///////////////////////////////////////////////////////////////////////////////
// Terminal
///////////////////////////////////////////////////////////////////////////////

void sig_int_handler(int ignore_){
	SIGNAL_INTERRUPT = true;
}

void signalResize(int ignore_) {
	SIGNAL_RESIZE = true;
}


// Setup the interrupt signal intercept
void setup_signal_handlers(){ 
	if(signal(SIGINT, SIG_IGN) != SIG_IGN){	
		if(signal(SIGINT, sig_int_handler) == SIG_ERR){
			throw std::runtime_error(" Error setting up signal handler!");
		}
	}

	//Handle resize signal
	signal(SIGWINCH, signalResize);

}
void Terminal::resize_() {
	//end session and then refresh to get new window sizes.
	endwin();
	refresh();

	//Mark resize as handled
	SIGNAL_RESIZE = false;

	//Get new window sizes
	getmaxyx(stdscr, _winSizeY, _winSizeX);

	//Make pad bigger if needed.
	int outputSizeY, outputSizeX;
	getmaxyx(output_window,outputSizeY,outputSizeX);
	if (outputSizeX < _winSizeX) {
		wresize(output_window,_scrollbackBufferSize,_winSizeX);
		wresize(input_window,1,_winSizeX);
		if (status_window) wresize(status_window,_statusWindowSize, _winSizeX);
	}

	//Update the physical cursor location
	cursY = _winSizeY - _statusWindowSize - 1;

}

void Terminal::pause(bool &flag) {
	endwin();
	std::cout.rdbuf(original); // Restore cout's original streambuf
	setvbuf(stdout,NULL,_IOLBF,0); //Change to line buffering
	while (flag) sleep(1);
	std::cout.rdbuf(pbuf); // Restore cout's original streambuf
	refresh_();
}

/**Refreshes the specified window or if none specified all windows.
 *
 *\param[in] window The pointer to the window to be updated.
 */
void Terminal::refresh_(){
	if(!init){ return; }
	if (SIGNAL_RESIZE) resize_();

	pnoutrefresh(output_window, 
		_scrollbackBufferSize - _winSizeY - _scrollPosition + 1, 0,  //Pad corner to be placed in top left 
		0, 0, _winSizeY - _statusWindowSize - 2, _winSizeX-1); //Size of window
	pnoutrefresh(input_window,0,0, _winSizeY - _statusWindowSize - 1, 0, _winSizeY - _statusWindowSize, _winSizeX-1);
	if (status_window) 
		pnoutrefresh(status_window,0,0, _winSizeY - _statusWindowSize, 0, _winSizeY, _winSizeX-1);

	refresh();

}

void Terminal::scroll_(int numLines){
	if (!init){return;}
	//We subtract so that a negative number is scrolled up resulting in a positive position value.
	_scrollPosition -= numLines;
	if(_scrollPosition > _scrollbackBufferSize - (_winSizeY-1)) 
		_scrollPosition = _scrollbackBufferSize - (_winSizeY-1);
	else if (_scrollPosition < 0) _scrollPosition = 0;
	refresh_();

}

void Terminal::update_cursor_(){
	if(!init){ return; }
	move(cursY, cursX); // Move the physical cursor
	wmove(input_window, 0, cursX); // Move the logical cursor in the input window
	refresh_();
}

void Terminal::clear_(){
	for(int start = cmd.GetSize() + offset; start >= offset; start--){
		wmove(input_window, 0, start);
		wdelch(input_window);
	}
	cmd.Clear();
	cursX = offset;
	text_length = 0;
	update_cursor_();
	refresh_();
}

/**Creates a status window and the refreshes the output. Takes an optional number of lines, defaulted to 1.
 *
 * \param[in] numLines Vertical size of status window.
 */
void Terminal::AddStatusWindow(unsigned short numLines) {
	_statusWindowSize = numLines;	
	//Create the new status pad.
	status_window = newpad(_statusWindowSize, _winSizeX);

	for (int i=0;i<numLines;i++) 
		statusStr.push_back(std::string(""));

	//Update the cursor position
	cursY = _winSizeY - _statusWindowSize - 1;
	update_cursor_();

	//Refresh the screen
	refresh_();
}

void Terminal::SetStatus(std::string status, unsigned short line) {
	ClearStatus(line);
	AppendStatus(status,line);
}

void Terminal::ClearStatus(unsigned short line) {
	if (status_window)
		statusStr.at(line) = "";
}

void Terminal::AppendStatus(std::string status, unsigned short line) {
	if (status_window)
		statusStr.at(line).append(status);
}

// Force a character to the input screen
void Terminal::in_char_(const char input_){
	cursX++;
	//If in insert mode we overwite the character otherwise insert it.
	if (cmd.GetInsertMode()) waddch(input_window, input_);
	else winsch(input_window, input_);
	update_cursor_();
	refresh_();
}

// Force text to the input screen
void Terminal::in_print_(const char* input_){
	cursX += cstrlen(input_);
	waddstr(input_window, input_);
	update_cursor_();
	refresh_();
}

void Terminal::init_colors_() {
	if(has_colors()) {
		start_color();
		//Use user's terminal colors.
		use_default_colors();

		//Define colors
		init_pair(1,COLOR_GREEN,-1);
		init_pair(2,COLOR_RED,-1);
		init_pair(3,COLOR_BLUE,-1);
		init_pair(4,COLOR_YELLOW,-1);
		init_pair(5,COLOR_MAGENTA,-1);
		init_pair(6,COLOR_CYAN,-1);
		init_pair(7,COLOR_WHITE,-1);
		
		//Assign colors to map
		attrMap[TermColors::DkGreen] = COLOR_PAIR(1);
		attrMap[TermColors::BtGreen] = COLOR_PAIR(1);
		attrMap[TermColors::DkRed] = COLOR_PAIR(2);
		attrMap[TermColors::BtRed] = COLOR_PAIR(2);
		attrMap[TermColors::DkBlue] = COLOR_PAIR(3);
		attrMap[TermColors::BtBlue] = COLOR_PAIR(3);
		attrMap[TermColors::DkYellow] = COLOR_PAIR(4);
		attrMap[TermColors::BtYellow] = COLOR_PAIR(4);
		attrMap[TermColors::DkMagenta] = COLOR_PAIR(5);
		attrMap[TermColors::BtMagenta] = COLOR_PAIR(5);
		attrMap[TermColors::DkCyan] = COLOR_PAIR(6);
		attrMap[TermColors::BtCyan] = COLOR_PAIR(6);
		attrMap[TermColors::DkWhite] = COLOR_PAIR(7);
		attrMap[TermColors::BtWhite] = COLOR_PAIR(7);
		attrMap[TermColors::Flashing] = A_BLINK;
		attrMap[TermColors::Underline] = A_UNDERLINE;
	}
}

bool Terminal::load_commands_(){
	std::ifstream input(cmd_filename.c_str());
	if(!input.good()){ return false; }
	
	size_t index;
	std::string cmd;
	std::vector<std::string> cmds;
	while(true){
		std::getline(input, cmd);
		if(input.eof()){ break; }
		
		// Strip the newline from the end
		index = cmd.find("\n");
		if(index != std::string::npos){
			cmd.erase(index);
		}
		
		// Push the command into the command array
		if(cmd != ""){ // Just to be safe!
			cmds.push_back(cmd);
		}
	}
	
	if(cmds.size() > 0){ // Push commands into the array in reverse order so that the original order is preserved
		std::vector<std::string>::iterator iter = cmds.end()-1;
		while(true){
			commands.Push(*iter);
			if(iter == cmds.begin()){ break; }
			else{ iter--; }
		}
	}
	
	input.close();
	return true;
}

bool Terminal::save_commands_(){
	std::ofstream output(cmd_filename.c_str());
	if(!output.good()){ return false; }
	
	std::string temp;
	unsigned int num_entries;
	if(commands.GetTotal() > commands.GetSize()){ num_entries = commands.GetSize(); }
	else{ num_entries = commands.GetTotal(); }
	
	for(unsigned int i = 0; i < num_entries; i++){
		temp = commands.GetPrev();
		if(temp != "NULL"){ // Again, just to be safe
			output << temp << std::endl;
		}
	}
	
	output.close();
	return true;
}

Terminal::Terminal() :
	status_window(NULL),
	_statusWindowSize(0),
	_scrollbackBufferSize(SCROLLBACK_SIZE),
	_scrollPosition(0)
{
	pbuf = NULL; 
	original = NULL;
	main = NULL;
	output_window = NULL;
	input_window = NULL;

	cmd_filename = "";
	init = false;
	save_cmds = false;
	text_length = 0;
	cursX = 0; 
	cursY = 0;
	offset = 0;
}

Terminal::~Terminal(){
	if(init){
		flush(); // Make sure no text is remaining in the buffer
		Close();
	}
}

void Terminal::Initialize(){
	if(init){ return; }
	
	original = std::cout.rdbuf(); // Back-up cout's streambuf
	pbuf = stream.rdbuf(); // Get stream's streambuf
	std::cout.flush();
	std::cout.rdbuf(pbuf); // Assign streambuf to cout
	
	main = initscr();
	
	if(main == NULL ){ // Attempt to initialize ncurses
		std::cout.rdbuf(original); // Restore cout's original streambuf
		fprintf(stderr, " Error: failed to initialize ncurses!\n");
	}
	else{		
   		getmaxyx(stdscr, _winSizeY, _winSizeX);
		output_window = newpad(_scrollbackBufferSize, _winSizeX);
		input_window = newpad(1, _winSizeX);
		wmove(output_window, _scrollbackBufferSize-1, 0); // Set the output cursor at the bottom so that new text will scroll up
		
		halfdelay(5); // Timeout after 5/10 of a second
		keypad(input_window, true); // Capture special keys
		noecho(); // Turn key echoing off
		
		scrollok(output_window, true);
		scrollok(input_window, true);

		if (NCURSES_MOUSE_VERSION > 0) {		
			mousemask(ALL_MOUSE_EVENTS,NULL);
			mouseinterval(0);
		}

		init = true;
		text_length = 0;
		offset = 0;
		
		// Set the position of the physical cursor
		cursX = 0; cursY = _winSizeY-1;
		update_cursor_();
		refresh_();

		init_colors_();
	}
	
	setup_signal_handlers();
}

void Terminal::Initialize(std::string cmd_fname_){
	if(init){ return; }
	
	Initialize();
	cmd_filename = cmd_fname_;
	save_cmds = true;
	load_commands_();
}

/// Set the command filename for storing previous commands
/// This command will clear all current commands from the history if overwrite_ is set to true
void Terminal::SetCommandFilename(std::string input_, bool overwrite_/*=false*/){
	if(save_cmds && !overwrite_){ return; }

	cmd_filename = input_;
	save_cmds = true;
	commands.Clear();
	load_commands_();
}

void Terminal::SetPrompt(const char *input_){
	prompt = input_;
	
	//Calculate the offset.
	//This is long winded as we want to ignore the escape sequences
	offset = 0;
	size_t pos = 0, lastPos = 0;
	while ((pos = prompt.find("\033[",lastPos)) != std::string::npos) {
		//Increase offset by number characters prior to escape
		offset += pos - lastPos;
		lastPos = pos;

		//Identify which escape code we found.
		//First try reset code then loop through other codes
		if (pos == prompt.find(TermColors::Reset,pos)) {
			lastPos += std::string(TermColors::Reset).length();
		}
		else {
			for(auto it=attrMap.begin(); it!= attrMap.end(); ++it) {
				//If the attribute is at the same position then we found this attribute and we turn it on
				if (pos == prompt.find(it->first,pos)) {
					//Iterate position to suppress printing the escape string
					lastPos += std::string(it->first).length();
					break;
				}
			}
		}	
	}
	offset += prompt.length() - lastPos;
	update_cursor_();

	print(input_window,prompt.c_str());
	refresh_();
}

// Force a character to the output screen
void Terminal::putch(const char input_){
	waddch(output_window, input_);
	refresh_();
}

// Force text to the output screen
void Terminal::print(WINDOW* window, std::string input_){
	size_t pos = 0, lastPos = 0;
	//Search for escape sequences
	while ((pos = input_.find("\033[",lastPos)) != std::string::npos) {
		//Output the string from last location to current escape sequence
		waddstr(window, input_.substr(lastPos,pos-lastPos).c_str());
		lastPos = pos;

		//Identify which escape code we found.
		//First try reset code then loop through other codes
		if (pos == input_.find(TermColors::Reset,pos)) {
			wattrset(window, A_NORMAL);
			lastPos += std::string(TermColors::Reset).length();
		}
		else {
			for(auto it=attrMap.begin(); it!= attrMap.end(); ++it) {
				//If the attribute is at the same position then we found this attribute and we turn it on
				if (pos == input_.find(it->first,pos)) {
					wattron(window, it->second);
					//Iterate position to suppress printing the escape string
					lastPos += std::string(it->first).length();
					break;
				}
			}
		}	
	}
	//Print the remaining string content
	waddstr(window, input_.substr(lastPos).c_str());
	refresh_();
}

// Dump all text in the stream to the output screen
void Terminal::flush(){
	std::string stream_contents = stream.str();
	if(stream_contents.size() > 0){
		print(output_window, stream_contents);
		if (logFile.good()) {
			logFile << stream.str();
			logFile.flush();
		}
		stream.str("");
		stream.clear();
	}
}

bool Terminal::SetLogFile(const char *logFileName) {
	logFile.open(logFileName,std::ofstream::app);
	if (!logFile.good()) {
		std::cout << "[ERROR]: Unable to open log file: "<< logFileName << "!\n";
		return false;
	}
	return true;

}

/**By enabling tab autocomplete the current typed command is returned via GetCommand() with a trailing tab character.
 *
 * \param[in] enable State of tab complete.
 */
void Terminal::EnableTabComplete(bool enable) {
	enableTabComplete = enable;
}

/**Take the list of matching tab complete values and output resulting tab completion.
 * If the list is empty nothing happens, if a unique value is given the command is completed. If there are multiple
 * matches the common part of the matches is determined and printed to the input. If there is no common part of the
 * matches and the tab key has been pressed twice the list of matches is printed for the user to decide.
 *
 * \param[in] matches A vector of strings of trailing characters matching the current command.
 */
void Terminal::TabComplete(std::vector<std::string> matches) {
	//No tab complete matches so we do nothing.
	if (matches.size() == 0) {
		return;
	}
	//A unique match so we extend the command with completed text
	else if (matches.size() == 1) {
		if (matches.at(0).find("/") == std::string::npos && (unsigned int) (cursX - offset) == cmd.Get().length()) {
			matches.at(0).append(" ");
		}
		cmd.Insert(cursX - offset, matches.at(0).c_str());
		waddstr(input_window, cmd.Get().substr(cursX - offset).c_str());
		cursX += matches.at(0).length();
		text_length += matches.at(0).length();
	}
	else {
		//Fill out the matching part
		std::string commonStr = matches.at(0);
		for (auto it=matches.begin()+1;it!=matches.end() && !commonStr.empty();++it) {
			while (!commonStr.empty()) {
				if ((*it).find(commonStr) == 0) break;
				commonStr.erase(commonStr.length() - 1);
			}
		}
		if (!commonStr.empty()) {
			cmd.Insert(cursX - offset, commonStr.c_str());
			waddstr(input_window, cmd.Get().substr(cursX - offset).c_str());
			cursX += commonStr.length();
			text_length += commonStr.length();
		}
		//Display the options
		else if (tabCount > 1) {
			//Compute the header position
			size_t headerPos = cmd.Get().find_last_of(" /",cursX - offset - 1);
			std::string header;
			if (headerPos == std::string::npos) 
				header = cmd.Get();
			else
				header = cmd.Get().substr(headerPos+1,cursX - offset - 1 - headerPos);
			std::cout << prompt.c_str() << cmd.Get() << "\n";
			for (auto it=matches.begin();it!=matches.end();++it) {
				std::cout << header << (*it) << "\t";
			}
			std::cout << "\n";
		}
	}
	update_cursor_();
	refresh_();
}

std::string Terminal::GetCommand(){
	std::string output = "";

	//Update status message
	if (status_window) {
		werase(status_window);
		print(status_window,statusStr.at(0).c_str());
	}

	while(true){
		if(SIGNAL_INTERRUPT){ // ctrl-c (SIGINT)
			SIGNAL_INTERRUPT = false;
			output = "CTRL_C";
			text_length = 0;
			break;
		}

		flush(); // If there is anything in the stream, dump it to the screen
		
		int keypress = wgetch(input_window);
	
		// Check for internal commands
		if(keypress == 10){ // Enter key (10)
			std::string temp_cmd = cmd.Get();
			if(temp_cmd != ""){ 
				//Reset the position in the history.
				commands.Reset();
				if(temp_cmd != commands.PeekPrev()){ // Only save this command if it is different than the previous command
					commands.Push(temp_cmd);
				}
				output = temp_cmd;
				std::cout << prompt.c_str() << output << "\n";
				flush();
				text_length = 0;
				_scrollPosition = 0;
				clear_();
				tabCount = 0;
				return output;
			}
		} 
		else if(keypress == '\t' && enableTabComplete) {
			tabCount++;
			output = cmd.Get().substr(0,cursX - offset) + "\t";
			return output;
		}
		else if(keypress == 4){ // ctrl-d (EOT)
			output = "CTRL_D";
			text_length = 0;
			clear_();
			tabCount = 0;
			break;
		}
		else if(keypress == 9){ } // Tab key (9)
		else if(keypress == KEY_UP){ // 259
			if(commands.GetIndex() == 0){ commands.Capture(cmd.Get()); }
			std::string temp_cmd = commands.GetPrev();
			if(temp_cmd != "NULL"){
				clear_();
				cmd.Set(temp_cmd);
				in_print_(cmd.Get().c_str());
				text_length = cmd.GetSize();
			}
		}
		else if(keypress == KEY_DOWN){ // 258
			std::string temp_cmd = commands.GetNext();
			if(temp_cmd != "NULL"){
				clear_();
				cmd.Set(temp_cmd);
				in_print_(cmd.Get().c_str());
				text_length = cmd.GetSize();
			}
		}
		else if(keypress == KEY_LEFT){ cursX--; } // 260
		else if(keypress == KEY_RIGHT){ cursX++; } // 261
		else if(keypress == KEY_PPAGE){ //Page up key
			scroll_(-(_winSizeY-2));
		}
		else if(keypress == KEY_NPAGE){ //Page down key
			scroll_(_winSizeY-2);
		}
		else if(keypress == KEY_BACKSPACE){ // 263
			wmove(input_window, 0, --cursX);
			wdelch(input_window);
			cmd.Pop(cursX - offset);
			text_length = cmd.GetSize();
		}
		else if(keypress == KEY_DC){ // Delete character (330)
			//Remove character from terminal
			wdelch(input_window);
			//Remove character from cmd string
			cmd.Pop(cursX - offset);
			text_length = cmd.GetSize();
		}
		else if(keypress == KEY_IC){ cmd.ToggleInsertMode(); } // Insert key (331)
		else if(keypress == KEY_HOME){ cursX = offset; }
		else if(keypress == KEY_END){ cursX = text_length + offset; }
		else if(keypress == KEY_MOUSE) { //Handle mouse events
			MEVENT mouseEvent;
			//Get information about mouse event.
			getmouse(&mouseEvent);
			
			switch (mouseEvent.bstate) {
				//Scroll up
				case BUTTON4_PRESSED:
					scroll_(-3);
					break;
				//Scroll down. (Yes the name is strange.)
				case REPORT_MOUSE_POSITION:
					scroll_(3);
					break;
			}
		}	
		else if(keypress == KEY_RESIZE) {
			//Do nothing with the resize key
		}
		else if(keypress == ERR){ } // No key was pressed in the interval
		else{ 
			in_char_((char)keypress); 
			cmd.Put((char)keypress, cursX - offset - 1);
			text_length = cmd.GetSize();
		}
		
		// Check for cursor too far to the left
		if(cursX < offset){ cursX = offset; }
	
		// Check for cursor too far to the right
		if(cursX > (text_length + offset)){ cursX = text_length + offset; }
	
		//Update status message
		if (status_window) {
			werase(status_window);
			print(status_window,statusStr.at(0).c_str());
		}

		if (keypress != ERR) tabCount = 0;
		update_cursor_();
		refresh_();
	}
	return output;
}

// Close the window and return control to the terminal
void Terminal::Close(){
	if(init){
		std::cout.rdbuf(original); // Restore cout's original streambuf
		delwin(output_window); // Delete the output window
		delwin(input_window); // Delete the input window
		delwin(main); // Delete the main window
		endwin(); // Restore Terminal settings
		
		if(save_cmds){ save_commands_(); }
		init = false;
	}
	logFile.close();
}
