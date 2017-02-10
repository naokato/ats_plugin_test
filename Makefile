BUILD_DIR = naokato
.PHONY: $(BUILD_DIR)

build: $(BUILD_DIR)
$(BUILD_DIR):
	$(MAKE) -C $@

cpplint:
	./cpplint.py naokato/*[c,cc,cpp,h,chh]
format:
	/usr/local/bin/clang-format -i naokato/*[c,cc,cpp,h,chh]
install:
	sudo cp naokato/libnaokato_ats_plugin.so /usr/local/libexec/trafficserver/
	sudo cp etc/plugin.config /usr/local/etc/trafficserver/plugin.config
	sudo trafficserver restart
