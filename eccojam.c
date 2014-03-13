/******************************************/
/*
 eccojam.c
 Eccojam Making Application
 by Mackenzie Starr 2013
 New York University
 
 See README.md for usage/description
 
 LibSndFile, LibSmpRate courtesy of Erik De Castro Lopo
 http://www.mega-nerd.com/libsndfile/
 http://www.mega-nerd.com/SRC/
 
*/
/******************************************/

#include <stdio.h>				
#include <stdlib.h>	
#include <string.h> 			
#include <sndfile.h>			
#include <samplerate.h>		
#include <ncurses.h>
#include <portaudio.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

#define SAMPLE                  float
#define TWO_PI					(2*M_PI)
#define FRAMES_PER_BUFFER       1024
#define SAMPLES_PER_BUFFER		(FRAMES_PER_BUFFER*2)
#define STEREO					2
#define SRC_RATIO_INC			0.1
#define DELAY_MAX				(FRAMES_PER_BUFFER * 50)
#define DELAY_AMP_INC			0.1
#define INIT_WIDTH              800
#define INIT_HEIGHT             600

//global Port Audio struct
PaStream *stream;


typedef struct _soundfile{

	//INFILE MEMBERS
	SNDFILE* infile;
	SF_INFO sfinfo;
	int size;
	int cursor;
	//for writing infile into memory
	SAMPLE *out_buffer;

	//RECORD FILE MEMBERS
	SNDFILE* recordfile;
	SF_INFO rec_info;
	char *record_file_name;
	bool record;

	//SRC MEMBERS
	SRC_STATE	*src_state ;
	SRC_DATA	src_data ;
	int		error ;
	SAMPLE 		src_input[FRAMES_PER_BUFFER * 100];
	SAMPLE          src_output[FRAMES_PER_BUFFER * 100];
	double 		src_ratio;
	int 		src_type;

	//LOOPING MEMBERS
	bool loop_in;
	bool loop_out;
	bool stop_loop;
	int restart;
	int in_point;
	int out_point;
	int looper;

	//DELAY MEMBERS
	bool delay_on;
	SAMPLE delay_read_amp;
	SAMPLE delay_read2_amp;
	SAMPLE delay_buf[DELAY_MAX];
	int delay_idx_read;
	int delay_idx_read2; 
	int delay_idx_write;
	int delay_length;

}sfData;

/******************************************
FUNCTION PROTOTYPES
******************************************/
static int paCallback( const void *inputBuffer,
			 void *outputBuffer, unsigned long framesPerBuffer,
			 const PaStreamCallbackTimeInfo* timeInfo,
			 PaStreamCallbackFlags statusFlags, void *userData );

void terminate_portaudio(PaStream *stream, PaError err);

int record_wav(sfData *data);

void print_header();

void print_ncurses_header();

void print_ncurses_help();

int src_algo_select();



/******************************************
MAIN
******************************************/
int main(int argc, char const *argv[])
{
	/*error checking for arguments*/
	if (argc !=2){
		printf("Usage: eccojam <audiofile>\n");
		return EXIT_FAILURE;
	}

  
	print_header();

	const char *infile = argv[1];

	//type struct sfData
	sfData data;

	//write zero to SF_INFO struct member
	memset(&data.sfinfo, 0, sizeof(data.sfinfo));

	//open SNDFILE pointer into data.infile
	data.infile = sf_open(infile, SFM_READ, &data.sfinfo);
		if(data.infile == NULL){
			printf("Error: could not open file: %s \n", infile);
			puts(sf_strerror (NULL));
			return EXIT_FAILURE;
		}

	//print sfinfo to the screen
	printf("\nFile info:\n"
			"\tname: %s\n"
			"\tlength: %.2fs\n"
			"\tsamplerate: %d\n"
			"\tchannels: %d\n"
			"\tformat: 0x%06x\n\n"
			,infile, 
			(float)data.sfinfo.frames/data.sfinfo.samplerate,
			data.sfinfo.samplerate, 
			data.sfinfo.channels, 
			data.sfinfo.format);

	

	data.src_type = src_algo_select();

	/*print number of frames*/
	printf("\nFrame Count: %d | ", (int)data.sfinfo.frames);


	//dynamically allocate enough space to hold all the samples of infile
	data.out_buffer = (SAMPLE*)malloc(sizeof(SAMPLE)
					  * data.sfinfo.frames * data.sfinfo.channels);
		//error check
		if(data.out_buffer == NULL){
			printf("Error: could not allocate enough memory for audio file %s", infile);
			return EXIT_FAILURE;	
		}

	
	//read in frames to out_buffer		  from          to               # of frames
	long sf_frame_count = sf_readf_float (data.infile, data.out_buffer, data.sfinfo.frames);
		//error check
		if (sf_frame_count != data.sfinfo.frames){
			printf("Error: not all frames of %s are in audio buffer 'out_buffer'",infile);
			return EXIT_FAILURE;
		}
		else{
			//print the frames and filename
  			printf("Read %ld frames from %s\n", sf_frame_count, infile);
  			data.size = sf_frame_count * data.sfinfo.channels;
  			sf_close(data.infile);
  		}
  	
  	printf("Samples: %d\n", data.size);

	PaError err;
	PaStreamParameters outputParameters;
	PaStream *stream;

	//initialize PortAudio
	err = Pa_Initialize();
		if (err != paNoError){
		printf("PortAudio error: %s\n", Pa_GetErrorText(err));
		exit(1);
		}	
		else{
		printf("\n...Port Audio Initialized...\n\n");
		}

	//print info about port audio
	printf( "'%s'\n\tversion # = %d\n\n",
 			 Pa_GetVersionText(), Pa_GetVersion() );


	//set outputParameters for PortAudio
    outputParameters.device = Pa_GetDefaultOutputDevice();
    outputParameters.channelCount = data.sfinfo.channels;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    /********************************************
	SfData Struct Defaults
	********************************************/

    	/*delay member defaults*/ 
    	data.delay_on = FALSE;
    	data.delay_read_amp = 0.5;
    	data.delay_read2_amp = 0.3;
    	data.delay_length = 21050;
		data.delay_idx_read = 0;
		data.delay_idx_read2 = 21050;
		data.delay_idx_write = 44100;
		memset (&data.delay_buf, 0, sizeof(float)*DELAY_MAX);
		/*src member defaults*/
    	data.src_ratio = 1.0;
    	/*looping member defaults*/
		data.loop_in = FALSE;
		data.loop_out = FALSE;
		data.stop_loop = FALSE;
		data.in_point = 0;
		data.out_point = 0;
		data.looper = 0;
		data.restart = 0;
		/*record member defaults*/
		data.record = FALSE;
		data.record_file_name = "default.wav";
		/*cursor for indexing through the audiofile*/
		data.cursor = 0;

	/********************************************/

	/* Initialize the sample rate converter. */
	if ((data.src_state = src_new (data.src_type, data.sfinfo.channels, &data.error)) == NULL)
	{	printf ("\n\nError : src_new() failed : %s.\n\n", src_strerror (data.error)) ;
		exit (1) ;
		} ;

	/* Settings for SRC_DATA Struct */
	data.src_data.data_in = data.src_input;
	data.src_data.input_frames = 0; //set later as FRAMES_PER_BUFFER / ratio
	data.src_data.data_out = data.src_output;
	data.src_data.output_frames = FRAMES_PER_BUFFER;
	data.src_data.src_ratio = data.src_ratio;

    //open I/O stream
	err = Pa_OpenStream(&stream, //write into stream
					    NULL, //no input channels
					    &outputParameters,
					    data.sfinfo.samplerate, //44100
						FRAMES_PER_BUFFER, //1024
						paNoFlag,
						paCallback, //callback function
					    &data); //address of structure

	if (err != paNoError){
		printf("PortAudio error: open stream - %s\n", Pa_GetErrorText(err));
		exit(1);
	}	

	//start PortAudio stream
	err = Pa_StartStream( stream );
	if (err != paNoError){
		printf("PortAudio error: start stream - %s\n", Pa_GetErrorText(err));
		exit(1);
	}



	/******************************************
	USER INPUT
	******************************************/

	initscr(); //start curses mode
	cbreak(); //line buffering disabled
	noecho(); //disable terminal echo, no characters print on screen
	curs_set(0); //make ncurses cursor invisible

	print_ncurses_header(); //print cool ASCII art header
	refresh();

	print_ncurses_help(); // print help menu 
	refresh();

	//default positions and values for ncurses menus
	mvprintw(7,1,"RECORDING: NO ");
	refresh();

	mvprintw(8,1,"DELAY: OFF");
	refresh();

	mvprintw(6,1,"LOOPING: OFF");
	refresh();

	mvprintw(6,50,"SPEED: %.2f",data.src_data.src_ratio);
	refresh();	

	mvprintw(9,1,"ecco #1 volume: %d", (int)(data.delay_read_amp*100));
	refresh();

	mvprintw(9,50,"ecco #2 volume: %d", (int)(data.delay_read2_amp*100));
	refresh();

	char ch = '\0';
	while (ch != 'q'){
		ch = getchar();
		
		switch (ch){

//LOOPER USER INPUTS

	//GIVE BOOL TO CALLBACK TO CAPTURE LOOP_IN POINT
		case 'z':
			data.loop_in = TRUE;
			break;
	//GIVE BOOL TO CALLBACK CAPTURE LOOP_OUT POINT
		case 'x':
			data.loop_out = TRUE;
			mvprintw(6,1,"LOOPING: ON ");
			refresh();
			break;
	//GIVE BOOL TO STOP LOOPING
		case 's':
			data.stop_loop = TRUE;
			mvprintw(6,1,"LOOPING: OFF");
			refresh();
			break;

//SRC USER INPUTS

		//INCREMENT SRC RATIO
		case ',':
				if (data.src_data.src_ratio <= 4.0){
				data.src_data.src_ratio += SRC_RATIO_INC;	
				mvprintw(6,50,"SPEED: %.2f",data.src_data.src_ratio);
				refresh();
				}	
			break;

		//DECREMENT SRC RATIO
		case '.':
				if (data.src_data.src_ratio >= 0.2 ){
				data.src_data.src_ratio -= SRC_RATIO_INC;
				mvprintw(6,50,"SPEED: %.2f",data.src_data.src_ratio);
				refresh();
				}
			break;

//RECORD USER INPUTS

		//TOGGLE RECORD ON / OFF
		case 'r':
			if (data.record == TRUE)
			{
				data.record = FALSE;
				sf_close(data.recordfile);
				mvprintw(7,1,"RECORDING: NO ");
				refresh();
			}
			else
			{
			record_wav(&data);
				mvprintw(7,1,"RECORDING: YES");
				refresh();
			}
			break;
		
//DELAY INPUTS 

		//TOGGLE BOOL FOR TOGGLING DELAY
		case 'd':
			if (data.delay_on == TRUE){
				data.delay_on = FALSE;
				mvprintw(8,1,"DELAY: OFF");
				refresh();
			}
			else{
				data.delay_on = TRUE;
				mvprintw(8,1,"DELAY: ON ");
				refresh();
			}
			break;

		// INC/DEC DELAY AMPLITUDES
		case 'f':
			if (data.delay_read_amp > 0.0){
				data.delay_read_amp -= DELAY_AMP_INC;
				mvprintw(9,1,"ecco #1 volume: %d", (int)(data.delay_read_amp*100));
				refresh();
			}
			break;
		case 'g':
			if (data.delay_read_amp < 0.8){
				data.delay_read_amp += DELAY_AMP_INC;
				mvprintw(9,1,"ecco #1 volume: %d", (int)(data.delay_read_amp*100));
				refresh();
			}
			break;
		case 'h':
			if (data.delay_read2_amp > 0.0){
				data.delay_read2_amp -= DELAY_AMP_INC;
				mvprintw(9,50,"ecco #2 volume: %d", (int)(data.delay_read2_amp*100));
				refresh();
			}
			break;
		case 'j':
			if (data.delay_read2_amp < 0.8){
				data.delay_read2_amp += DELAY_AMP_INC;
				mvprintw(9,50,"ecco #2 volume: %d", (int)(data.delay_read2_amp*100));
				refresh();
			}
			break;
		}
	}
	

	endwin(); //end curses mode

	/******************************************/
	
//FREE ALL DYNAMIC MEMORY ALLOCATED
	terminate_portaudio( stream, err );
	free (data.out_buffer);
	src_delete(data.src_state);
	return EXIT_SUCCESS;
}

static int paCallback( const void *inputBuffer,
			 void *outputBuffer, unsigned long framesPerBuffer,
			 const PaStreamCallbackTimeInfo* timeInfo,
			 PaStreamCallbackFlags statusFlags, void *userData )
{
	//cast unused variables
	(void)inputBuffer;
	(void)timeInfo;
	(void)statusFlags;

	//cast used variables
	float *out = (float*)outputBuffer;
	sfData *data = (sfData*)userData;

	/* This loop writes out_buffer[] into src_input[] for x
	number of frames given by (# of samples)/(src_ratio)*/
	int j;
	for (j=0; j < (int)(FRAMES_PER_BUFFER * data->sfinfo.channels)/data->src_data.src_ratio; j++){

		//IF USER INPUT is 'z', stores current cursor position as the loop in point
		if (data->loop_in){
			data->in_point = data->cursor;
			data->loop_in = FALSE;
			data->looper++;

			}

		//IF USER INPUT is 'x', stores the current cursor as the loop out point
		if (data->loop_out){
			data->out_point = data->cursor;
			data->loop_out = FALSE;
			data->restart = data->in_point;
			data->looper++;
		}

		//If 'z' and 'x' have been pressed, looping startes
		if ((data->looper != 0) && (data->looper%2 == 0)){
			data->src_input[j] = data->out_buffer[data->in_point];
			data->in_point++;
			/*go until the loop_out point and restart at loop_in point*/
			if (data->in_point == data->out_point){
				data->in_point = data->restart;
			}
			/*If USER INPUT 's' the loop stops*/
			if(data->stop_loop){
				data->looper = 0;
				data->stop_loop = FALSE;
			}
		}

		//Normal mode (sans looping)
		else{
			data->src_input[j] = data->out_buffer[data->cursor];
			data->cursor+=1;
			if(data->size <= data->cursor){
				data->cursor = 0;
			}
		}
	}

	/*Give src_proccess() the number of input frames to process*/
	data->src_data.input_frames = FRAMES_PER_BUFFER/data->src_data.src_ratio + 1;
	/*Let src_process() know there are still buffers to process*/
	data->src_data.end_of_input = 0;

	/* Process src_input buffer and empty results in src_output buffer*/
	if ((data->error = src_process (data->src_state, &data->src_data)))
		{	printf ("\nError : %s\n", src_strerror (data->error)) ;
			exit (1) ;
			} ;

	/*left for further testing purposes*/
	// printf("input frames used: %li\n", data->src_data.input_frames_used);
	// printf("output frames gen: %li\n", data->src_data.output_frames_gen);


	int i;
	for (i=0; i < (FRAMES_PER_BUFFER * data->sfinfo.channels); i++){
		
		/*if the delay is toggled*/

		if (data->delay_on){
			data->delay_buf[data->delay_idx_write] = data->src_output[i];
			out[i] = data->src_output[i] + (data->delay_buf[data->delay_idx_read] *
					 data->delay_read_amp) + (data->delay_buf[data->delay_idx_read2] *
					 data->delay_read2_amp);

		}

		else{
			out[i] = data->src_output[i];
		}

		 /* Increase indices */
        data->delay_idx_read++;
        data->delay_idx_read2++;
        data->delay_idx_write++;
        /* Wrap around the buffer */
        data->delay_idx_read %= DELAY_MAX;
        data->delay_idx_read2 %= DELAY_MAX;
        data->delay_idx_write %= DELAY_MAX;	

       

	}

	//if USER INPUT 'r' this statement is toggled
	if(data->record){
		sf_writef_float(data->recordfile, out, framesPerBuffer);
		}

	// g_ready = true;
	return paContinue;
}



void terminate_portaudio(PaStream *stream, PaError err){
	
	//stop PortAudio stream
	err = Pa_StopStream( stream );
    if (err != paNoError) {
        printf(  "PortAudio error: stop stream - %s\n", Pa_GetErrorText(err));
        exit(1);
    }
    //close PortAudio stream
    err = Pa_CloseStream( stream );
    if (err != paNoError) {
        printf("PortAudio error: close stream: %s\n", Pa_GetErrorText(err));
        exit(1);
    }
	//terminate PortAudio
	err = Pa_Terminate();
	 if (err != paNoError) {
        printf("PortAudio error: close stream: %s\n", Pa_GetErrorText(err));
        exit(1);
    }
}


int record_wav(sfData *data){

	 /* Setup sfinfo for new audio file */
    memset(&data->rec_info, 0, sizeof(SF_INFO));
    data->rec_info.samplerate = data->sfinfo.samplerate;
    data->rec_info.channels = data->sfinfo.channels;
    data->rec_info.format = data->sfinfo.format;
    if (!sf_format_check(&data->rec_info)) {
        printf("Error: Incorrect audio file format");
        return EXIT_FAILURE;
    }

    /* Open new audio file */
    if (( data->recordfile = sf_open( data->record_file_name, SFM_WRITE, &data->rec_info ) ) == NULL ) {
        printf("Error, couldn't open the file\n");
        return EXIT_FAILURE;
    }
    /* set record boolean to trigger statement in callback*/
    else{
    	data->record = TRUE;
    }
    return 0;
}


void print_header(){
	printf(
	"                                           _/\n"                                      
    "   _/_/      _/_/_/    _/_/_/     _/_/          _/_/_/   _/_/_/  _/_/     _/_/_/\n"   
    " _/_/_/_/  _/        _/        _/    _/  _/  _/    _/  _/    _/    _/  _/_/\n"        
    "_/        _/        _/        _/    _/  _/  _/    _/  _/    _/    _/      _/_/\n"     
    " _/_/_/    _/_/_/    _/_/_/    _/_/    _/    _/_/_/  _/    _/    _/  _/_/_/\n"        
    "                                      _/                                  \n"         
    "                                     _/\n");
	}

//-----------------------------------------------------------------------------
// Name: print_ncurses_header( )
// Desc: cool ASCII art header
//-----------------------------------------------------------------------------

void print_ncurses_header(){
	mvprintw(0,0,
	"                                           _/\n"                                      
    "   _/_/      _/_/_/    _/_/_/     _/_/          _/_/_/   _/_/_/  _/_/     _/_/_/\n"   
    " _/_/_/_/  _/        _/        _/    _/  _/  _/    _/  _/    _/    _/  _/_/\n"        
    "_/        _/        _/        _/    _/  _/  _/    _/  _/    _/    _/      _/_/\n"     
    " _/_/_/    _/_/_/    _/_/_/    _/_/    _/    _/_/_/  _/    _/    _/  _/_/_/\n"        
    "                                      _/                                  \n"         
    "                                     _/\n");
	}

//-----------------------------------------------------------------------------
// Name: print_ncurses_help( )
// Desc: prints help menu to ncurses interface
//-----------------------------------------------------------------------------

void print_ncurses_help(){
	mvprintw(15,0,
	"--------------------------------------------------------------------------------\n"
	"ECCOJAM HELP MENU                                                               \n"
	"--------------------------------------------------------------------------------\n"
	"Press \'R\' to start recording, \'R\' again to stop\n"
	"Press \'D\' to turn on delay, \'D\' again to toggle off\n"
	"Press \'Z\' / \'X\' to set loop in / loop out points and start looping\n"
	"Press \'S\' to stop loop mode\n"
	"Press \'<\' / \'>\' to speed up and slow down file\n"
	"Press \'F\'/\'G\' & \'H\'/\'J\' to control the volumes of the two echo 'tape heads'\n"
	"Press 'Q' to quit the program\n"
	"--------------------------------------------------------------------------------\n");
	}


//-----------------------------------------------------------------------------
// Name: src_algo_select( )
// Desc: returns integer 0-4 for libsmprate src algorithm
//-----------------------------------------------------------------------------

int src_algo_select(){

	printf("\nSelect which sample rate conversion algorithm you would like to use:\n"
			"\t[0] SRC_SINC_BEST_QUALITY\n"
			"\t[1] SRC_SINC_MEDIUM_QUALITY\n"
			"\t[2] SRC_SINC_FASTEST\n"
			"\t[3] SRC_ZERO_ORDER_HOLD\n"
			"\t[4] SRC_LINEAR\n\n"
			);

	printf("...");
	char c = getchar();
	/* error checking for valid range */
	while(c < '0' || c > '4'){
		printf("...");
		c = getchar();
		if (c >= '0' && c <= '4'){
			break;
		}
		printf("\nSRC choice is out of valid range [0:4], please try again.\n\n");
	}
		
	return (int)(c-'0');
}
