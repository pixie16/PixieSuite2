/** \file ScanMain.hpp
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
#include <string>
#include <sstream>
#include <vector>

#include "hribf_buffers.h"

#define SCAN_VERSION "1.2.09"
#define SCAN_DATE "April 25th, 2016"

class Server;
class Terminal;
class Unpacker;

class fileInformation{
  private:
	std::vector<std::string> parnames;
	std::vector<std::string> parvalues;

  public:
	fileInformation(){ }
	
	~fileInformation(){ }
	
	size_t size(){ return parnames.size(); }
	
	bool at(const size_t &index_, std::string &name, std::string &value);
	
	template <typename T>
	bool push_back(const std::string & name_, const T &value_, const std::string &units_="");
	
	bool is_in(const std::string & name_);
	
	std::string print(const size_t &index_);
};

class ScanMain{
  private:
	unsigned int maxShmSizeL; /// Max size of shared memory buffer in pixie words (4050 + 2 header words)
	unsigned int maxShmSize; /// Max size of shared memory buffer in bytes

	std::string prefix; /// Input filename prefix (without extension).
	std::string extension; /// Input file extension.

	int max_spill_size; /// Maximum size of a spill to read.
	int file_format; /// Input file format to use (0=.ldf, 1=.pld, 2=.root).
	
	unsigned long num_spills_recvd; /// The total number of good spills received from either the input file or shared memory.
	unsigned long file_start_offset; /// The first word in the file at which to start scanning.
	
	bool write_counts; ///

	bool total_stopped;
	bool is_running; /// Set to true if the acqusition is running.
	bool is_verbose; /// Set to true if the user wishes verbose information to be displayed.
	bool debug_mode; /// Set to true if the user wishes to display debug information.
	bool dry_run_mode; /// Set to true if a dry run is to be performed i.e. data is to be read but not processed.
	bool shm_mode; /// Set to true if shared memory mode is to be used.
	bool batch_mode; /// Set to true if the program is to be run with no interactive command line.
	bool init;

	bool kill_all; /// Set to true when user has sent kill command.
	bool run_ctrl_exit; /// Set to true when run control thread has exited.

	Server *poll_server; /// Poll2 shared memory server.

	std::ifstream input_file; /// Main input binary data file.
	std::streampos file_length; /// Main input file length (in bytes).

	fileInformation finfo; /// Data structure for storing binary file header information.

	PLD_header pldHead; /// PLD style HEAD buffer handler.
	PLD_data pldData; /// PLD style DATA buffer handler.
	DIR_buffer dirbuff; /// HRIBF DIR buffer handler.
	HEAD_buffer headbuff; /// HRIBF HEAD buffer handler.
	DATA_buffer databuff; /// HRIBF DATA buffer handler.
	EOF_buffer eofbuff; /// HRIBF EOF buffer handler.

	Terminal *term; /// ncurses terminal used for displaying output and handling user input.

	Unpacker *core; /// Pointer to class derived from Unpacker class.
  
	std::string sys_message_head; /// The string to print before program output.
  
	unsigned int split_str(std::string str_, std::vector<std::string> &args, char delimiter_=' ');
	
	std::string get_extension(std::string filename_, std::string &prefix);

	void start_scan();
	
	void stop_scan();

  public:
	ScanMain(Unpacker *core_=NULL);
	
	~ScanMain();

	bool IsInit(){ return init; }

	bool IsVerbose(){ return is_verbose; }
	
	bool DebugMode(){ return debug_mode; }
	
	bool DryRunMode(){ return dry_run_mode; }
	
	bool ShmMode(){ return shm_mode; }
	
	std::string GetMessageHeader(){ return sys_message_head; }
	
	fileInformation *GetFileInfo(){ return &finfo; }

	void SetMessageHeader(const std::string &head_){ sys_message_head = head_; }
	
	bool SetVerboseMode(bool state_=true){ return (is_verbose = state_); }
	
	bool SetDebugMode(bool state_=true){ return (debug_mode = state_); }
	
	bool SetDryRunMode(bool state_=true){ return (dry_run_mode = state_); }
	
	bool SetShmMode(bool state_=true){ return (shm_mode = state_); }
	
	bool SetBatchMode(bool state_=true){ return (batch_mode = state_); }
	
	void RunControl();
	
	void CmdControl();
	
	void Help(char *name_, Unpacker *core_);
	
	bool Initialize(int argc, char *argv[]);
	
	bool Rewind();
	
	int Execute();
	
	bool Close();
};
