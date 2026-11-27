// SPDX-License-Identifier: GPL-2.0-or-later

#include <CtrSdMailbox.h>

unsigned int SdProbe(unsigned int *last_lba);
unsigned int SdRead(unsigned int lba, volatile unsigned int *output);
unsigned int SdPresent(void);
unsigned int SdRemoved(void);

static void barrier(void)
{
    unsigned int zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" : : "r"(zero) : "memory");
}

void SdService(void)
{
    volatile CTR_SD_MAILBOX *mail = (volatile CTR_SD_MAILBOX *)CTR_SD_MAILBOX_ADDRESS;
    unsigned int ready = 0;
    unsigned int last_lba = 0;
    unsigned int generation = 0;

    for (;;) {
        unsigned int request = mail->Request;
        unsigned int result;

        if (request == CTR_SD_IDLE)
            continue;

        if (ready && SdRemoved()) {
            ready = 0;
            generation++;
        }

        if (request == CTR_SD_PROBE) {
            result = ready ? CTR_SD_OK : SdProbe(&last_lba);
            if (!ready && result == CTR_SD_OK) {
                ready = 1;
                generation++;
            }
        } else if (request == CTR_SD_READ) {
            if (!SdPresent()) {
                result = CTR_SD_ABSENT;
            } else if (!ready) {
                result = CTR_SD_CHANGED;
            } else if (mail->Lba > last_lba) {
                result = CTR_SD_IO_ERROR;
            } else {
                result = SdRead(mail->Lba, mail->Data);
            }
        } else {
            result = CTR_SD_IO_ERROR;
        }

        mail->LastLba = last_lba;
        mail->Generation = generation;
        mail->Result = result;
        barrier();
        mail->Request = CTR_SD_IDLE;
        barrier();
    }
}
