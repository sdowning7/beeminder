//hive process file.
//reads data from CSV for temperature and humidity, performs FFT on audio sample,
//compares to thresholds
//and adds flags to CSV.

 #include <stdio.h>
 #include <stdlib.h>
 #include <errno.h>
 #include <stdint.h>
 #include <string.h>
 #include <sys/mman.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <math.h>
 #include <sys/time.h>
 #include <time.h>
 
 #include "kiss_fft.h"
 #include "kiss_fftr.h"
 
 #define OFFSET 27
 
 #define HUMIDITY_LOW 4500
#define HUMIDITY_HIGH 6500
#define TEMP_LOW 9300
#define TEMP_HIGH 9600

 #define RIFF 0x46464952
 #define WAVE 0x45564157
 #define FMT 0x20746d66
 #define DATA 0x61746164
 #define PCM 1

#define OK_FLAG 0
#define LOW_FLAG 1
#define HIGH_FLAG 2

#define FREQ_4DAY_BEE 285
#define FREQ_6DAY_BEE 225
#define FREQ_9DAY_BEE 190
#define FREQ_QUEEN 400
#define SAMPLERATE 16000

#define PI 3.141592653

//  struct wave_header {
//    uint32_t ChunkID;
//    uint32_t ChunkSize;
//    uint32_t Format;
//    uint32_t Subchunk1ID;
//    uint32_t Subchunk1Size;
//    uint16_t AudioFormat;
//    uint16_t NumChannels;
//    uint32_t SampleRate;
//    uint32_t ByteRate;
//    uint16_t BlockAlign;
//    uint16_t BitsPerSample;
//    uint32_t Subchunk2ID;
//    uint32_t Subchunk2Size;
//  };

struct raw_hivedata{
	uint32_t weight;
	uint32_t humidity;
	uint32_t temperature;
};
struct hivedata{
	uint32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t hour;
	uint32_t minute;
	uint32_t humidity;
	uint32_t temperature;
	uint8_t humidity_flag;
	uint8_t temperature_flag;
	uint8_t bee_flags[4];
};

//read wave header
 int read_hivedata(FILE *fp, struct raw_hivedata *dest) {
   if (!dest || !fp) {
     return -ENOENT;
   }

   fseek(fp, 0, SEEK_SET);
   fread(dest, sizeof(struct raw_hivedata), 1, fp);
   return 0;
 }
 
 //handles temperature, humidity, and timestamp reading into hive struct
 //as well as comparing to threshold values predetermined by bee science :)
 void th_handle(struct raw_hivedata *raw_hive, struct hivedata *hive) {

	hive->weight = raw_hive->weight;
	hive->temperature = raw_hive->temperature;
	hive->humidity = raw_hive->humidity;

	if(hive->humidity > HUMIDITY_HIGH) {
		hive->humidity_flag = HIGH_FLAG;
	}
	else if (hive->humidity < HUMIDITY_LOW) {
		hive->humidity_flag = LOW_FLAG;
	}
	else {
		hive->humidity_flag = OK_FLAG;
	}
	
	if(hive->temperature > TEMP_HIGH) {
		hive->temperature_flag = HIGH_FLAG;
	}
	else if (hive->temperature < TEMP_LOW) {
		hive->temperature_flag = LOW_FLAG;
	}
	else {
		hive->temperature_flag = OK_FLAG;
	}
 }

//handles the FFT process for the entire WAVE file.   
void FFT_handle(FILE *fp, float* master_fft_array) {
	kiss_fftr_cfg cfg = kiss_fftr_alloc(11024, 0, NULL, NULL);
	int16_t int_buf[11024];
	float buf[11024];
	size_t nr;
	kiss_fft_cpx fft_output[11024/2+1];
	unsigned int start = sizeof(struct raw_hivedata);
	fseek(fp, start, SEEK_SET);
	
	//loop through the whole file
	while(!feof(fp)) {
		//read into the buffer
		nr = fread(int_buf, 1, sizeof(int_buf), fp);
		if (nr != sizeof(int_buf)) {
			break;
		}
		//window the buffer
		for(int i = 0; i < sizeof(int_buf)/2; i++) {
			buf[i] = (float)int_buf[i]*0.5*(1-cos(2*PI*i/11023));
		}
		//run fft on this window, calculate magnitudes and add it to master array
		kiss_fftr(cfg, buf, fft_output);
		for(int i = 0; i < sizeof(fft_output)/8; i++) {
			float magnitude = sqrt(pow(fft_output[i].r, 2)+pow(fft_output[i].i, 2));
			if(i==1000) {
			}
			master_fft_array[i] += magnitude;
		}
		//advance the start offset for the next loop by half a window length
		start += 11024/2*sizeof(float);
		//go to the next position
		fseek(fp, start, SEEK_SET); 
	}
	kiss_fftr_free(cfg);
	fclose(fp);
}

//compares fft output of the file to expected values and modifies hive data file
//with flags corresponding to levels of desired frequencies.
int audio_compare(float *fft_array, int numsamples, struct hivedata *hive) {
	
	//define thresholds for bee age distribution as percentages of total population
	//4daybee_low, 4daybee_high, 6daybee_low, 6daybee_high, 9daybee_low, 9daybee_high
	float bee_age_dist[] = {17.9, 28.1, 34.6, 47.5, 33.2, 41.6};

	//frequency bin size is 4Hz
	int freq_bin_size = hdr->SAMPLERATE/numsamples;
	//calculate amplitudes of different bee ages
	float amp_4day_bee = fft_array[FREQ_4DAY_BEE/freq_bin_size];
	float amp_6day_bee = fft_array[FREQ_6DAY_BEE/freq_bin_size];
	float amp_9day_bee = fft_array[FREQ_9DAY_BEE/freq_bin_size];
	float amp_queen = fft_array[FREQ_QUEEN/freq_bin_size];
	//the 3 age ranges only cover about 80% of the population of the hive.
	//assuming the other 20% of the bees are still in the hive, 
	//we will need to scale the amplitude by a factor of 1.2.
	//since the queen is only 1 bee, we do not include her in this calculation.
	float amp_total_bee = 1.2*(amp_4day_bee + amp_6day_bee + amp_9day_bee);
	for (int i = 0; i < 4; i++) {
		hive->bee_flags[i] = OK_FLAG;
	}
	
	//get age distribution as percentages and flag
	float amp_4day_bee_percentage = (float)amp_4day_bee/amp_total_bee*100;
	float amp_6day_bee_percentage = (float)amp_6day_bee/amp_total_bee*100;
	float amp_9day_bee_percentage = (float)amp_9day_bee/amp_total_bee*100;
	if (amp_4day_bee_percentage < bee_age_dist[0]) {
		hive->bee_flags[0] = LOW_FLAG;
	}
	else if(amp_4day_bee_percentage > bee_age_dist[1]) {
		hive->bee_flags[0] = HIGH_FLAG;
	}
	if (amp_6day_bee_percentage < bee_age_dist[2]) {
		hive->bee_flags[1] = LOW_FLAG;
	}
	else if(amp_6day_bee_percentage > bee_age_dist[3]) {
		hive->bee_flags[1] = HIGH_FLAG;
	}
	if (amp_9day_bee_percentage < bee_age_dist[4]) {
		hive->bee_flags[2] = LOW_FLAG;
	}
	else if(amp_9day_bee_percentage > bee_age_dist[5]) {
		hive->bee_flags[2] = HIGH_FLAG;
	}
	
	//check presence of queen.  if her amplitude is higher 
	//than the amplitude of much higher frequencies (which we would not expect to see much of)
	//we can say we have detected a queen.  Flag for presence is 0 for false, 1 for true.
	if (amp_queen > 2*fft_array[FREQ_QUEEN*2]) {
		hive->bee_flags[3] = 1;
	}
	
	printf("4 Day Bee percentage: %f\n", amp_4day_bee_percentage);
	printf("6 Day Bee percentage: %f\n", amp_6day_bee_percentage);
	printf("9 Day Bee percentage: %f\n", amp_9day_bee_percentage);
	printf("Queen in hive?: %d\n", hive->bee_flags[3]);
	return 0;
} 

 int main(int argc, char** argv) {
   FILE* fp;
   struct hivedata hive;
   struct raw_hivedata raw_hive;
   
   float fft_array[11024];
   FFT_handle(fp, fft_array);
   read_hivedata(fp, raw_hive);
   th_handle(&raw_hive, &hive);
   audio_compare(fft_array, sizeof(fft_array)/4, &hive);
   fseek(hivefp, 0, SEEK_END);
   for (int i = 0; i < 4; i++) {
		fprintf(hivefp, "%d ", hive.bee_flags[i]);
	}
	fprintf(hivefp, "\n");

	fclose(hivefp);
 }
