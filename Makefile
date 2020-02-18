TARGETS=ringmaster player

all: $(TARGETS)
clean:
	rm -f $(TARGETS)

ringmaster: ring_master.cpp
	g++ -g -o $@ $<

player: player.cpp
	g++ -g -o $@ $<


