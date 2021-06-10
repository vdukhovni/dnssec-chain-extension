VERSION = 08
DOCNAME = draft-dukhovni-tls-dnssec-chain
today := $(shell TZ=UTC date +%Y-%m-%dT00:00:00Z)

all: $(DOCNAME)-$(VERSION).txt $(DOCNAME)-$(VERSION).html

$(DOCNAME)-$(VERSION).txt: $(DOCNAME).xml
	xml2rfc --text -o $@ $<
	sed -i -e 's/&lt;/</g' -e 's/&gt;/>/g' $(DOCNAME)-$(VERSION).txt

$(DOCNAME)-$(VERSION).html: $(DOCNAME).xml
	xml2rfc --html -o $@ $<

$(DOCNAME).xml: $(DOCNAME).md
	sed -e 's/@DOCNAME@/$(DOCNAME)-$(VERSION)/g' \
	    -e 's/@TODAY@/${today}/g'  $< | mmark > $@ || rm -f $@
	sed -i -e 's/&quot;/"/g' -e 's/<[\/]*bcp14>//g' -e 's/&amp;/&/g' $(DOCNAME).xml

clean:
	rm -f $(DOCNAME).xml $(DOCNAME)-$(VERSION).txt $(DOCNAME)-$(VERSION).html

