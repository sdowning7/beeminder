OBJS = json-parser/json.o json_builder.o kiss_fft.o kiss_fftr.o hive_process.o

KISS_DIR = kissfft
KISS_TOOL_DIR = $(KISS_DIR)/tools
JSON_DIR = json-parser
JSON_BUILD_DIR = json-builder

INCDIRS = -I$(KISS_DIR) -I$(KISS_TOOL_DIR) -I$(JSON_DIR) -I$(JSON_BUILD_DIR)

CFLAGS += -g

hive_process: $(OBJS)
	$(CC) $(INCDIRS) $(OBJS) -o hive_process -lm

hive_process.o: hive_process.c
	$(CC) $(CFLAGS) -c $(INCDIRS) $< -o $@

kiss_fftr.o: $(KISS_TOOL_DIR)/kiss_fftr.c
	$(CC) $(CFLAGS) -c $(INCDIRS) $< -o $@

kiss_fft.o: $(KISS_DIR)/kiss_fft.c
	$(CC) $(CFLAGS) -c $(INCDIRS) $< -o $@

json_builder.o: $(JSON_BUILD_DIR)/json-builder.c
	$(CC) $(CFLAGS) -c $(INCDIRS) $< -o $@

clean:
	rm -f hive_process $(OBJS)
