TESTS=$(sort $(basename $(wildcard generated*.lama)))
rapidlama=../../rapidlama
LAMAC=lamac

.PHONY: check $(TESTS)

check: $(TESTS)

$(TESTS): %: %.lama
	@echo "regression/expressions/$@"
	@$(LAMAC) -b $<
	@cat $@.input | $(rapidlama) $@.bc > $@.log && diff $@.log orig/$@.log

clean:
	rm -f *.log *.s *.bc *~
	find . -maxdepth 1 -type f -not -name '*.*' -not -name 'Makefile' -delete

