LIBS=-lcurl

pubsub.so: pubsub.cc
	CXXFLAGS=$(CXXFLAGS) LIBS=$(LIBS) znc-buildmod $<

install: pubsub
	mkdir -p $(HOME)/.znc/modules/
	cp pubsub.so $(HOME)/.znc/modules/pubsub.so

clean:
	-rm -f pubsub.so
