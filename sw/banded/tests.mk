# keep the fixed values large enough to be meaningful without making every run drag.
FIXED_SEQ_LEN := 128
FIXED_NUM_SEQ := 64
FIXED_COL := 4
FIXED_BAND := 64

# sweep band size while holding the other parameters fixed.
TESTS += $(call test-name,$(FIXED_SEQ_LEN),8,$(FIXED_NUM_SEQ),$(FIXED_COL))
TESTS += $(call test-name,$(FIXED_SEQ_LEN),16,$(FIXED_NUM_SEQ),$(FIXED_COL))
TESTS += $(call test-name,$(FIXED_SEQ_LEN),32,$(FIXED_NUM_SEQ),$(FIXED_COL))
TESTS += $(call test-name,$(FIXED_SEQ_LEN),64,$(FIXED_NUM_SEQ),$(FIXED_COL))
TESTS += $(call test-name,$(FIXED_SEQ_LEN),128,$(FIXED_NUM_SEQ),$(FIXED_COL))

# sweep col while holding the other parameters fixed.
TESTS += $(call test-name,$(FIXED_SEQ_LEN),$(FIXED_BAND),$(FIXED_NUM_SEQ),1)
TESTS += $(call test-name,$(FIXED_SEQ_LEN),$(FIXED_BAND),$(FIXED_NUM_SEQ),2)
TESTS += $(call test-name,$(FIXED_SEQ_LEN),$(FIXED_BAND),$(FIXED_NUM_SEQ),8)
TESTS += $(call test-name,$(FIXED_SEQ_LEN),$(FIXED_BAND),$(FIXED_NUM_SEQ),16)
