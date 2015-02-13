#ifndef HRIBF_BUFFERS_H
#define HRIBF_BUFFERS_H

#include <fstream>

class BufferType{
  protected:
	int bufftype;
	int buffsize;
	int buffend;
	int zero;
	bool debug_mode;
	
	BufferType(int bufftype_, int buffsize_, int buffend_=-1);
	
	// Returns only false if not overwritten
	virtual bool Write(std::ofstream *file_);
	
  public:
	void SetDebugMode(bool debug_=true){ debug_mode = debug_; }
};

// The DIR buffer is written at the beginning of each .ldf file. When the file is ready
// to be closed, the data within the DIR buffer is re-written with run information.
class DIR_buffer : public BufferType{
  private:
  	int total_buff_size;
	int run_num;
	int unknown[3];
	
  public:
	DIR_buffer();
	
	void SetRunNumber(int input_){ run_num = input_; }
	
	// DIR buffer (1 word buffer type, 1 word buffer size, 1 word for total buffer length,
	// 1 word for total number of buffers, 2 unknown words, 1 word for run number, 1 unknown word,
	// and 8186 zeros)
	bool Write(std::ofstream *file_);
};

// The HEAD buffer is written after the DIR buffer for each .ldf file. HEAD contains information
// about the run including the date/time, the title, and the run number.
class HEAD_buffer : public BufferType{
  private:
	char facility[8];
	char format[8];
	char type[16];
	char date[16];
	char run_title[80];
	int run_num;

	void set_char_array(std::string input_, char *arr_, unsigned int size_);
	
  public:
	HEAD_buffer();
		
	bool SetDateTime();
	
	bool SetTitle(std::string input_);
	
	void SetRunNumber(int input_){ run_num = input_; }

	// HEAD buffer (1 word buffer type, 1 word buffer size, 2 words for facility, 2 for format, 
	// 3 for type, 1 word separator, 4 word date, 20 word title [80 character], 1 word run number,
	// 30 words of padding, and 8129 end of buffer words)
	bool Write(std::ofstream *file_);
};

// The DATA buffer contains all physics data within the .ldf file
class DATA_buffer : public BufferType{
  private:
	unsigned int current_buff_pos; // Absolute buffer position
	unsigned int buff_words_remaining; // Absolute number of buffer words remaining
	unsigned int good_words_remaining; // Good buffer words remaining (not counting header or footer words)

	// DATA buffer (1 word buffer type, 1 word buffer size)
	bool open_(std::ofstream *file_);
	
  public:
	DATA_buffer(); // 0x41544144 "DATA"

	// Close a data buffer by padding with 0xFFFFFFFF
	bool Close(std::ofstream *file_);
	
	// Write data to file
	bool Write(std::ofstream *file_, char *data_, unsigned int nWords_, unsigned int &buffs_written, unsigned int output_format_=0);
};

// A single EOF buffer signals the end of a run (pacman .ldf format). A double EOF signals the end of the .ldf file.
class EOF_buffer : public BufferType{	
  public:
	EOF_buffer() : BufferType(541478725, 8192){} // 0x20464F45 "EOF "
	
	// EOF buffer (1 word buffer type, 1 word buffer size, and 8192 end of buffer words)
	bool Write(std::ofstream *file_);
};

class PollOutputFile{
  private:
	std::ofstream output_file;
	std::string fname_prefix;
	DIR_buffer dirBuff;
	HEAD_buffer headBuff;
	DATA_buffer dataBuff;
	EOF_buffer eofBuff;
	unsigned int current_file_num;
	unsigned int output_format;
	unsigned int total_num_buffer;
	unsigned long total_num_bytes;
	bool debug_mode;

	/* Get the formatted filename of the current file. */
	std::string get_filename();

	// Overwrite the fourth word of the file
	// The fourth word sets the total number of buffers which the file contains
	bool overwrite_dir();

  public:
	PollOutputFile();

	PollOutputFile(std::string filename_);
	
	~PollOutputFile(){ CloseFile(); }
	
	void SetDebugMode(bool debug_=true);
	
	bool SetFileFormat(unsigned int format_);

	void SetFilenamePrefix(std::string filename_);

	bool IsOpen(){ return output_file.is_open(); }
	
	int Write(char *data_, unsigned int nWords_);

	// Close the current file, if one is open, and open a new file for data output
	bool OpenNewFile(std::string title_, unsigned int run_num_, std::string &current_fname, std::string output_dir="./");

	// Write the footer and close the file
	void CloseFile();
};

#endif