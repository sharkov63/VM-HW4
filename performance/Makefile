TESTS=$(sort $(basename $(wildcard *.lama)))
rapidlama=../rapidlama
LAMAC=lamac

.PHONY: check $(TESTS)

check: $(TESTS)

$(TESTS): %: %.lama
	@echo $@
	@$(LAMAC) -b $<
	@$(LAMAC) $<
	`which time` -f "$@\t%U" ./$@
	`which time` -f "$@\t%U" $(rapidlama) $@.bc
	`which time` -f "$@\t%U" $(LAMAC) -i $< < /dev/null

clean:
	$(RM) test*.log *.s *~ $(TESTS) *.i
