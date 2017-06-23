//---------------------------------------------------------------------------
//Software written by Scott Swift 2017 - This program is distributed under the
//terms of the GNU General Public License.
//---------------------------------------------------------------------------
#pragma package(smart_init)
#include <vcl.h>
#pragma hdrstop

#include "MainForm.h"
#include "RegHelper.h"
//#include <Clipbrd.hpp>
#include <stdio.h>
#include <math.h>
//---------------------------------------------------------------------------
#pragma link "AdPort"
#pragma link "OoMisc"
#pragma resource "*.dfm"
TFormMain *FormMain;
//---------------------------------------------------------------------------
__fastcall TFormMain::TFormMain(TComponent* Owner)
    : TForm(Owner)
{
#if (AKI_FILE_HEADER_SIZE != 72)
    printm("\r\nWARNING: sizeof(PSTOR) != 72\r\n"
        "TO DEVELOPER: sizeof(PSTOR) MUST be " + String(72) + " bytes\r\n"
        " to maintain compatibility with old .AKI files!");
#endif
#if (sizeof(S950CAT) != 12)
    printm("\r\nWARNING: sizeof(S950CAT) != 12\r\n"
        "TO DEVELOPER: sizeof(S950CAT) MUST be " + String(12) + " bytes!");
#endif
#if TESTING
    printm("\r\nWARNING: TEST VERSION!!!!");
#endif
    // use the following # for PSTOR_STRUCT_SIZ in MainForm.h!
    //printm("sizeof(PSTOR):" + String(sizeof(PSTOR)));
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::FormCreate(TObject *Sender)
{
    Timer1->OnTimer = NULL;
    Timer1->Enabled = false;

    m_rxByteCount = 0;
    m_rxTimeout = false;
    m_txTimeout = false;
    m_gpTimeout = false;
    m_sysBusy = false;
    m_numSampEntries = 0;
    m_numProgEntries = 0;
    m_DragDropFilePath = "";
    m_inBufferFull = false;
    m_abort = false;

    // read settings from registry HKEY_CURRENT_USER
    // \\Software\\Discrete-Time Systems\\AkaiS950
    TRegHelper* pReg = NULL;

    try
    {
        try
        {
            pReg = new TRegHelper(true);

            if (pReg != NULL)
            {
                // tell user how to delete reg key if this is first use...
                if (pReg->ReadSetting(S9_REGKEY_VERSION).IsEmpty())
                {
                    pReg->WriteSetting(S9_REGKEY_VERSION, String(VERSION_STR));

                    // cmd reg delete "HKCU\Software\Discrete-Time Systems\AkaiS950" /f
                    printm("This app stores its settings in the windows registry.\r\n"
                        "To delete settings, go to Start => Run and type \"cmd\"\r\n"
                        "In the window type the line below and press enter:\r\n\r\n"
                        "reg delete \"HKCU\\Software\\Discrete-Time Systems\\AkaiS950\" /f\r\n"
                        "(or: Start => Run, \"regedit\" and search for \"AkaiS950\")\r\n");
                }

                pReg->ReadSetting(S9_REGKEY_BAUD, m_baud, DEF_RS232_BAUD_RATE);
                pReg->ReadSetting(S9_REGKEY_USE_RIGHT_CHAN, m_use_right_chan, true);
                pReg->ReadSetting(S9_REGKEY_AUTO_RENAME, m_auto_rename, true);
                pReg->ReadSetting(S9_REGKEY_FORCE_HWFLOW, m_force_hwflow, false);
            }
            else
            {
                ShowMessage("Unable to read settings from the registry!");
                m_baud = DEF_RS232_BAUD_RATE;
                m_use_right_chan = true;
                m_auto_rename = true;
                m_force_hwflow = false;
            }
        }
        catch (...)
        {
            ShowMessage("Unable to read settings from the registry!");
            m_baud = DEF_RS232_BAUD_RATE;
            m_use_right_chan = true;
            m_auto_rename = true;
            m_force_hwflow = false;
        }

        ComboBoxRs232->Enabled = true;

        m_data_size = DATA_PACKET_SIZE;
        m_data_size += DATA_PACKET_OVERHEAD;
        m_hedr_size = HEDRSIZ;
        m_ack_size = ACKSIZ;

        printm(VERSION_STR);
        printm("Click \"Menu\" and select \"Help\"...");
    }
    __finally
    {
        try { if (pReg != NULL) delete pReg; }
        catch (...) {}
    }

    // in the ApdComPort1 component, set:
    //HWFlowOptions:
    //hwUseDTR = false;
    //hwUseRTS = false;
    //hwRequireDSR = false;
    //hwRequireCTS = false;
    //SWFlowOptions = swfNone;

    MenuUseRightChanForStereoSamples->Checked = m_use_right_chan;
    MenuAutomaticallyRenameSample->Checked = m_auto_rename;
    MenuUseHWFlowControlBelow50000Baud->Checked = m_force_hwflow;
    SetMenuItems();

    ComboBoxRs232->Text = String(m_baud);
    SetComPort(m_baud);

    //enable drag&drop files
    ::DragAcceptFiles(this->Handle, true);
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::FormShow(TObject *Sender)
{
    Memo1->SetFocus();
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::FormClose(TObject *Sender, TCloseAction &Action)
{
    // save settings to registry HKEY_CURRENT_USER
    // \\Software\\Discrete-Time Systems\\AkaiS950
    TRegHelper* pReg = NULL;

    try
    {
        pReg = new TRegHelper(false);

        if (pReg != NULL)
        {
            pReg->WriteSetting(S9_REGKEY_BAUD, m_baud);
            pReg->WriteSetting(S9_REGKEY_USE_RIGHT_CHAN, m_use_right_chan);
            pReg->WriteSetting(S9_REGKEY_AUTO_RENAME, m_auto_rename);
            pReg->WriteSetting(S9_REGKEY_FORCE_HWFLOW, m_force_hwflow);
        }
    }
    __finally
    {
        try { if (pReg != NULL) delete pReg; }
        catch (...) {}
    }
}
//---------------------------------------------------------------------------
// Get sample routines
//---------------------------------------------------------------------------
// all you need to call to receive a sample into a file...
// returns 0 if success. Pass in the index from sampler's catalog
int __fastcall TFormMain::GetSample(int samp, String fileName)
{
    int iFileHandle;

    if (!FileExists(fileName))
        iFileHandle = FileCreate(fileName);
    else
        iFileHandle = FileOpen(fileName, fmShareDenyNone | fmOpenReadWrite);

    if (iFileHandle == 0)
    {
        printm("error opening file, bad handle!");
        return 4;
    }

    UInt32 my_magic = MAGIC_NUM_AKI;
    PSTOR ps = { 0 };

    try
    {
        // request common reception enable (no delay)
        if (!exmit(0, SECRE, false))
            return 1; // tx timeout

        // populate global samp_parms array
        if (get_comm_samp_parms(samp))
            return 1; // could not get sample parms

        if (get_comm_samp_hedr(samp))
            return 2; // could not get header data

        // fill global PSTOR struct...
        decode_sample_info(&ps);

        print_ps_info(&ps);

        // write the magic number and header...
        FileWrite(iFileHandle, (char*)&my_magic, UINT32SIZE);
        // write the full 72 byte struct
        FileWrite(iFileHandle, (char*)&ps, AKI_FILE_HEADER_SIZE);

        // For midi, the in and out buffers must both be open at this point!
        if (get_samp_data(&ps, iFileHandle))
            return 3; // could not get sample data
    }
    __finally
    {
        // request common reception disable (after 25ms delay)
        exmit(0, SECRD, true);

        if (iFileHandle != 0)
            FileClose(iFileHandle);
    }

    return 0;
    // 1=no parms, 2=no header, 3=no samp data, 4=bad file handle
}
//---------------------------------------------------------------------------
// puts samp_parms[] and samp_hedr[] arrays info into PSTOR struct (ps)
void __fastcall TFormMain::decode_sample_info(PSTOR* ps)
{
    // FROM AKAI EXCLUSIVE SAMPLE PARAMETERS... (do this before decoding header)

    // clear spare-byte fields
    for (int ii = 0; ii < PSTOR_SPARE_COUNT; ii++)
        ps->spares[ii] = (Byte)0;
    ps->spareint1 = 0;
    ps->spareint2 = (Byte)0;
    ps->sparechar1 = (Byte)0;

    // ASCII sample name
    decode_parmsDB((Byte*)ps->name, &samp_parms[7], MAX_NAME_S900); // 20
    trimright(ps->name); // trim off the blanks

    // undefined
    // ps->undef_dd = decode_parmsDD((Byte*)&samp_parms[27]); // 8

    // undefined
    // ps->undef_dw = decode_parmsDW((Byte*)&samp_parms[35]); // 4

    // number of words in sample (for velocity-crossfade it's the sum of soft and loud parts)
    ps->totalct = decode_parmsDD(&samp_parms[39]); // 8

    // original sample rate in Hz
    ps->freq = decode_parmsDW(&samp_parms[47]); // 4

    // nominal pitch in 1/16 semitone, C3=960
    ps->pitch = decode_parmsDW(&samp_parms[51]); // 4

    // loudness offset (signed) nominal pitch in 1/16 semitone, C3=960
    ps->loudnessoffset = decode_parmsDW(&samp_parms[55]); // 4

    // replay mode 'A', 'L' or 'O' (alternating, looping or one-shot)
    // (samp_hedr[18] also has sample's loop mode: 0=looping, 1=alternating
    // or one-shot if loop-length < 5)
    ps->loopmode = decode_parmsDB(&samp_parms[59]); // 2

    // 61,62 DB reserved
    //ps->reserved = decode_parmsDB(&samp_parms[61]); // 2

    // 63-70 DD end point relative to start of sample (1800 default for TONE sample)
    ps->endpoint = decode_parmsDD(&samp_parms[63]);

    // 71-78 DD first replay point relative to start of sample (default is 0)
    ps->loopstart = decode_parmsDD(&samp_parms[71]);

    // 79-86 DD length of looping or alternating part
    ps->looplen = decode_parmsDD(&samp_parms[79]);

    // 87-90 DW reserved

    // 91,92 DB type of sample 0=normal, 255=velocity crossfade
    bool xfade = (decode_parmsDB(&samp_parms[91]) == (Byte)255); // 2

    // 93,94 DB sample waveform 'N'=normal, 'R'=reversed
    bool reversed = (decode_parmsDB(&samp_parms[93]) == 'R'); // 2

    ps->flags = 0;
    if (xfade)
        ps->flags |= (Byte)1;
    if (reversed)
        ps->flags |= (Byte)2;
    //if (???)
    //  ps->flags |= (Byte)4;
    //if (???)
    //  ps->flags |= (Byte)8;
    //if (???)
    //  ps->flags |= (Byte)16;
    //if (???)
    //  ps->flags |= (Byte)32;
    //if (???)
    //  ps->flags |= (Byte)64;
    //if (???)
    //  ps->flags |= (Byte)128;

    // 95-126 (4 DDs) undefined

    // FROM SAMPLE HEADER...

    // bits per sample-word (S900 transmits 12 but can accept 8-14)
    // S950 can accept 16 but not serially, only from S1000 disk????
    ps->bits_per_word = (UInt16)samp_hedr[5]; // 4

    // sampling period in nS 15259-500000
    ps->period = decode_hedrTB(&samp_hedr[6]); // 3

    // NOW USING VALUES IN samp_parms FOR ITEMS BELOW!!!!!!!!!!!!!!!!!!

    // number of sample words 200-475020
    // ps->totalct = decode_hedrTB((Byte*)&samp_hedr[9]); // 3

    // loop start point (non-looping mode if >= endidx-5)
    // int loopstart = decode_hedrTB((Byte*)&samp_hedr[12]); // 3

    // loop end point (S950/S900 takes this as end point of the sample)
    // int loopend = decode_hedrTB((Byte*)&samp_hedr[15]); // 3

    // ps->startidx = loopstart;
    // ps->endidx = loopend;
    // ps->looplen = loopend - loopstart;

    // we directly get this from samp_parms[59] instead!
    // samp_hedr[18] is mode 0=looping, 1=alternating (one-shot if loop-length < 5)
    //if (ps->endidx-ps->loopidx < 5)
    //    ps->looping = 'O';
    //else
    //    ps->looping = (samp_hedr[18] & (Byte)0x01) : 'A' : 'L';
}
//---------------------------------------------------------------------------
// get a sample's header info into the global TempArray
int __fastcall TFormMain::get_comm_samp_hedr(int samp)
{
    // request sample dump (after 25ms delay)
    int mode = RSD;
    if (!cxmit(samp, mode, true))
        return 1; // rx timeout

    // wait for data
    if (receive(m_hedr_size))
    {
        printm("timeout receiving sample header!");
        return 1;
    }

    // copy first 19 bytes of TempArray into samp_hedr buffer
    memcpy(samp_hedr, TempArray, m_hedr_size);

    return 0; // 0=OK, 1=com error
}
//---------------------------------------------------------------------------
// returns 0=OK, 1=writerror, 2=com error, 4=bad chksm, 8=wrong # words, 16=bad # bits per sample
// receive sample data blocks and write them to a file, print a progress display
// all functions here print their own error messages ao you can just
// look for a return value of 0 for success.
int __fastcall TFormMain::get_samp_data(PSTOR* ps, int handle)
{
    // always 120 byte packets (plus block-count and checksum), S900 is
    // max 60 2-byte words in 14-bits and S950 is max 40 3-byte words in 16-bits
    // (but the S950 seems NOT to be able to receive 16-bit samples as it
    // turns out though the manual says it can process 16-bits from disk???)
    // (The S900/S950 protocol was designed to be compliant with the Prophet 2000
    // which is supposed to handle 16-bit sample packets as 40 3-byte words or
    // 20 4-byte words... (only the lower 7-bits of each byte is usable))
    //
    // we recieve any # bits per word and properly format it as 40 or 60
    // 16-bit 2's compliment sample-words in rbuf
    __int16 *rbuf = NULL;
    int status = 0;

    try
    {
        unsigned count = 0;
        int writct, retstat;
        String dots = "";
        int blockct = 0;

        int bits_per_word = ps->bits_per_word;

        int bytes_per_word = bits_per_word / 7;
        if (bits_per_word % 7)
            bytes_per_word++;

        if (DATA_PACKET_SIZE % bytes_per_word)
        {
            printm("can't fit expected samples into 120 byte packets: " +
                                                        String(bits_per_word));
            status = 16; // bad bits-per-sample
            return status;
        }

        // will be 2 words (120/60 for 8-14 bit samples) for S900/S950, [but we
        // could also handle 3 words (120/40 15-21 bit samples) or
        // 4 words (120/20 22-28 bit samples)]
        int words_per_block = DATA_PACKET_SIZE / bytes_per_word;

        unsigned total_words = ps->totalct;

        rbuf = new __int16[words_per_block];

        printm("bits per sample-word: " + String(bits_per_word));
        printm("bytes per sample-word: " + String(bytes_per_word));
        printm("sample-words per data-block: " + String(words_per_block));

        for (;;)
        {
            if (!chandshake(ACKS, blockct))
                break;

            // if NOT using midi we have to send the last ACK (above) to tell
            // the machine to send the EEX (which we just ignore)
            if (count >= ps->totalct)
                break;

            // (midi out and in must be opened before calling this!)
            // (blockct passed is only used for displaying messages...)
            if ((retstat = get_comm_samp_data(rbuf, bytes_per_word,
                        words_per_block, bits_per_word, blockct)) != 0)
            {
                if (retstat == 1)
                    status = 2; // comm error
                else if (retstat == 2)
                    status = 4; // checksum error

                break; // timeout or error
            }

            if (ps->totalct <= 19200)
            {
                if (blockct % 8 == 0)
                {
                    dots += ".";
                    printm(dots);
                }
            }
            else
            {
                if (blockct % 32 == 0)
                {
                    dots += ".";
                    printm(dots);
                }
            }

            count += words_per_block;
            writct = words_per_block * UINT16SIZE; // size in rbuf always 16-bits!

            // write only up to totalct words...
            if (count > total_words)
                writct -= (int)(count - total_words) * UINT16SIZE;

            // write to file
            if (FileWrite(handle, rbuf, writct) < 0)
            {
                chandshake(ASD); // abort dump
                printm("error writing sample data to file! (block=" +
                                                   String(blockct) + ")");
                status = 1;
                break;
            }

            blockct++;
        }

        if (count < total_words)
        {
            if (count == 0)
                printm("no sample data received!");
            else
                printm("expected " + String(total_words) +
                        " bytes, but received " + String(count) + "!");

            status |= 8; // wrong number of words received
        }
    }
    __finally
    {
        if (rbuf)
            delete[] rbuf;
    }

    return status;
}
//---------------------------------------------------------------------------
// returns: 0=OK, 1=receive error, 2=bad checksum
// receive 8-16-bit sample data into 16-bit words
// converts a block of raw sample-data from the machine in 7-bit midi-format,
// and moves it from TempArray into bufptr; also validates the checksum.
//
// # words per 120 byte block is generally 60 8-14 bit samples
// or 40 15-21 bit samples... could be 20 22-28 bit samples
//
// (midi out and in must both be open before calling this!)
int __fastcall TFormMain::get_comm_samp_data(__int16* dest,
    int bytes_per_word, int words_per_block, int bits_per_word, int blockct)
{
    int bc = blockct+1; // display 1 greater...

    try
    {
        // receive data sample data block from serial port
        // and store in tempBuf
        if (receive(m_data_size))
        {
            FormMain->printm("did not receive expected " +
                        String(m_data_size) +
                " byte data-block! (block=" + String(bc) + ")");
            FormMain->printm("(receiver error code is: " +
                                        String(m_rxByteCount) + ")");
            return 1;
        }

        Byte *cp;
        Byte checksum = 0;

        cp = TempArray + 1;

        __int16 baseline = (__int16)(1 << (bits_per_word - 1));

        // process tempBuf into a data buffer and validate checksum
        // (NOTE: tricky little algorithm I came up with that handles
        // data bytes that come in like the following:
        //
        // ps->bits_per_word == 16 bits
        // byte 1 = 0 d15 d14 d13 d12 d11 d10 d09 (shift_count = 16-7 = 9)
        // byte 2 = 0 d08 d07 d06 d05 d04 d03 d02 (shift_count = 16-14 = 2)
        // byte 3 = 0 d01 d00  0   0   0   0   0  (shift_count = 16-21 = -5)
        //
        // ps->bits_per_word == 14 bits
        // byte 1 = 0 d13 d12 d11 d10 d09 d08 d07 (shift_count = 14-7 = 7)
        // byte 2 = 0 d06 d05 d04 d03 d02 d01 d00 (shift_count = 14-14 = 0)
        //
        // ps->bits_per_word == 12 bits
        // byte 1 = 0 d11 d10 d09 d08 d07 d06 d05 (shift_count = 12-7 = 5)
        // byte 2 = 0 d04 d03 d02 d01 d00  0   0  (shift_count = 12-14 = -2)
        //
        // ps->bits_per_word == 8 bits
        // byte 1 = 0 d07 d06 d05 d04 d03 d02 d01 (shift_count = 8-7 = 1)
        // byte 2 = 0 d00  0   0   0   0   0   0  (shift_count = 8-14 = -6)
        //
        // ...and turns it into a 16-bit two's compliment sample-point
        // to store in a file.
        for (int ii = 0; ii < words_per_block; ii++)
        {
            __int16 tempint = 0;

            for (int jj = 1; jj <= bytes_per_word; jj++)
            {
                int shift_count = bits_per_word - (jj * 7);

                UInt16 val = (UInt16)*cp;
                checksum ^= *cp++;

                UInt16 or_val;
                if (shift_count >= 0)
                    or_val = (UInt16)(val << shift_count);
                else
                    or_val = (UInt16)(val >> -shift_count);

                tempint |= or_val;
            }

            tempint -= baseline; // convert to two's compliment
            *dest++ = tempint;
        }

        if (checksum != *cp)
        {
            printm("bad checksum! (block=" + String(bc) + ")");
            return 2; // bad checksum
        }

        return 0;
    }
    catch(...)
    {
        return 1;
    }
}
//---------------------------------------------------------------------------
// returns: 0=OK, 1=receive error, 2=wrong bytes, 3=bad checksum
// gets a sample's sysex extended parameters and puts them in the
// global TempArray... also validates the checksum
int __fastcall TFormMain::get_comm_samp_parms(int samp)
{
    try
    {
        // request sample parms (after 25ms delay)
        // (NOTE: this will open both in/out ports as needed!)
        if (!exmit(samp, RSPRM, true))
            return 1; // tx timeout

        // receive complete SYSEX message from serial port..., put into TempArray
        if (receive(0))
        {
            printm("timeout receiving sample parameters!");
            return 1;
        }

        if (m_rxByteCount != PARMSIZ)
        {
            printm("received wrong bytecount for sample parameters: " + String(m_rxByteCount));
            return 2;
        }

        Byte* cp = TempArray + 7; // point past header to sample-name
        Byte checksum = 0;

        // checksum of buffer minus 7-byte header and checksum and EEX
        for (int ii = 0; ii < PARMSIZ - 9; ii++)
            checksum ^= *cp++;

        if (checksum != *cp)
        {
            printm("bad checksum for sample parameters!");
            return 3; // bad checksum
        }

        // copy TempArray into samp_parms
        memcpy(samp_parms, TempArray, PARMSIZ);
    }
    catch(...)
    {
        printm("exception in get_comm_samp_parms()!");
        return 1;
    }

    return 0;
}
//---------------------------------------------------------------------------
Byte __fastcall TFormMain::decode_parmsDB(Byte* source)
{
    Byte c = *source++;
    c |= (Byte)(*source << 7);
    return c;
}
//---------------------------------------------------------------------------
// make sure sizeof dest buffer >= numchars+1!
void __fastcall TFormMain::decode_parmsDB(Byte* dest, Byte* source, int numchars)
{
    for (int ii = 0; ii < numchars; ii++)
    {
        *dest = *source++;
        *dest |= (Byte)(*source++ << 7);
        dest++;
    }

    if (numchars > 1)
        *dest = '\0';
}
//---------------------------------------------------------------------------
// decode a 32-bit value in 8-bytes into an __int32
UInt32 __fastcall TFormMain::decode_parmsDD(Byte* tp)
{
    UInt32 acc;

    tp += 6;

    acc = 0;
    acc |= *tp | (*(tp + 1) << 7);

    for (int ii = 0; ii < 3; ii++)
    {
        acc <<= 8;
        tp -= 2;
        acc |= *tp | (*(tp + 1) << 7);
    }

    return acc;
}
//---------------------------------------------------------------------------
// decode a 16-bit value in 4-bytes into an __int32
UInt32 __fastcall TFormMain::decode_parmsDW(Byte* tp)
{
    UInt32 acc;

    tp += 2;

    acc = 0;
    acc |= *tp | (*(tp + 1) << 7);

    acc <<= 8;
    tp -= 2;
    acc |= *tp | (*(tp + 1) << 7);

    return acc;
}
//---------------------------------------------------------------------------
// decode a 21-bit value in 3-bytes into an __int32
UInt32 __fastcall TFormMain::decode_hedrTB(Byte* tp)
{
    UInt32 acc;

    tp += 2;

    acc = (UInt32)*tp--;
    acc = (acc << 7) | (UInt32)*tp--;
    acc = (acc << 7) | (UInt32)*tp;

    return acc;
}
//---------------------------------------------------------------------------
// Send sample routines
//---------------------------------------------------------------------------
bool __fastcall TFormMain::PutSample(String sFilePath)
{
    ListBox1->Clear();
    Memo1->Clear();

    int iFileLength;
    int iBytesRead;
    int sampIndex;
    PSTOR ps = { 0 };
    Byte newname[MAX_NAME_S900 + 1];

    // vars released in __finally block
    Byte *fileBuf = NULL;
    Byte *tbuf = NULL;
    int iFileHandle = 0;

    try
    {
        //printm("allocated " + String(iFileLength+1) + " bytes...");
        try
        {
            if (sFilePath.IsEmpty() || !FileExists(sFilePath))
            {
                printm("file does not exist!");
                return false;
            }

            // Load file
            printm("file path: \"" + sFilePath + "\"");

            // allow file to be opened for reading by another program at the same time
            iFileHandle = FileOpen(sFilePath, fmShareDenyNone | fmOpenRead);

            if (iFileHandle == 0)
            {
                printm("unable to open file, handle is 0!");
                return false;
            }

            // get file length
            iFileLength = FileSeek(iFileHandle, 0, 2);
            FileSeek(iFileHandle, 0, 0); // back to start

            if (iFileLength == 0)
            {
                FileClose(iFileHandle);
                printm("unable to open file, length is 0!");
                return false;
            }

            fileBuf = new Byte[iFileLength + 1];

            if (fileBuf == NULL)
            {
                FileClose(iFileHandle);
                printm("unable to allocate " + String(iFileLength + 1) +
                                                         " bytes of memory!");
                return false;
            }

            try
            {
                iBytesRead = FileRead(iFileHandle, fileBuf, iFileLength);
                //printm("read " + String(iBytesRead) + " bytes...");
            }
            catch (...)
            {
                printm("unable to read file into buffer...");
                return false;
            }

            // finished with the file...
            FileClose(iFileHandle);
            iFileHandle = 0;

            // copy up to the first 10 chars of file-name without extension
            // into newname and pad the rest with spaces, terminate
            String sName = ExtractFileName(sFilePath);

            String Ext = ExtractFileExt(sName).LowerCase();

            if (Ext.IsEmpty() || (Ext != ".aki" && Ext != ".wav"))
            {
                ShowMessage("File \"" + sName + "\" must have a\n.aki or .wav file-extension!");
                return false;
            }

            // Form a max 10 char sample name from the file's name
            int lenName = sName.Length();
            bool bStop = false;
            for (int ii = 1; ii <= MAX_NAME_S900; ii++)
            {
                if (!bStop && (ii > lenName || sName[ii] == '.'))
                    bStop = true;

                if (bStop)
                    newname[ii - 1] = ' ';
                else
                    newname[ii - 1] = sName[ii];
            }
            // terminate it
            newname[MAX_NAME_S900] = '\0';

            printm("rs232 baud-rate: " + String(m_baud));

            int machine_max_bits_per_word = S900_BITS_PER_WORD;

            int bytes_per_word = machine_max_bits_per_word / 7;

            if (machine_max_bits_per_word % 7)
                bytes_per_word++;

            // # sample-words is 120/2 = 60 for 8-14 bit samples,
            // 120/3 = 40 for 15-21 bit samples and 120/4 = 20
            // for 22-28 bit samples. there should be no remainder!
            if (DATA_PACKET_SIZE % bytes_per_word)
            {
                printm("bytes_per_word of " + String(bytes_per_word) +
                    " does not fit evenly into 120 byte packet!");
                return false;
            }

            int words_per_block = DATA_PACKET_SIZE / bytes_per_word;

            // transmit buffer (allow for checksum and block number, 122 bytes)
            // (127 bytes if sample-dump standard mode)
            tbuf = new Byte[m_data_size];

            // preload the data packet buffer with sample-dump-standard
            // constants if SDS was selected from menu...
            // For SDS, the checksum is the running XOR of all the data after
            // the SYSEX byte, up to but not including the checksum itself.
            // F0 7E cc 02 kk <120 bytes> mm F7
            Byte initial_checksum = 0;

            Byte *ptbuf; // transmit buffer pointer
            Byte checksum;
            int blockct;

            bool bSendAborted = false; // flag set if we receive a not-acknowledge on any data-packet

            m_abort = false; // user can press ESC a key to quit

            if (Ext.IsEmpty() || (Ext != ".aki" && Ext != ".wav"))
            {
                ShowMessage("File \"" + sName + "\" must have a\n.aki or .wav file-extension!");
                return false;
            }

            if (Ext != ".aki") // Not an AKI file? (try WAV...)
            {
                if (iBytesRead < 45)
                {
                    printm("bad file (1)");
                    return false;
                }

                if (!StrCmpCaseInsens((char*)&fileBuf[0], "RIFF", 4))
                {
                    printm("bad file (2) [no \'RIFF\' preamble!]");
                    return false;
                }

                int file_size = *(__int32*)&fileBuf[4];
                if (file_size + 8 != iBytesRead)
                {
                    printm("bad file (3), (file_size = " +
                        String(file_size + 8) + ", iBytesRead = " +
                        String(iBytesRead) + ")!");
                    return false;
                }

                if (!StrCmpCaseInsens((char*)&fileBuf[8], "WAVE", 4))
                {
                    printm("bad file (4) [no \'WAVE\' preamble!]");
                    return false;
                }

                // Search file for "fmt " block
                Byte* headerPtr = fileBuf;
                // NOTE: the FindSubsection will return headerPtr by reference!
                __int32 headerSize = FindSubsection(headerPtr, "fmt ", iBytesRead);
                if (headerSize < 0)
                {
                    printm("bad file (4) [no \'fmt \' sub-section!]");
                    return false;
                }

                // Length of the format data in bytes is a four-byte int at
                // offset 16. It should be at least 16 bytes of sample-info...
                if (headerSize < 16)
                {
                    printm("bad file (6) [\'fmt \' sub-section is < 16 bytes!]");
                    return false;
                }

                // Search file for "data" block
                // (Left-channel data typically begins after the header
                // at 12 + 4 + 4 + 16 + 4 + 4 - BUT - could be anywhere...)
                Byte* dataPtr = fileBuf;
                // NOTE: the FindSubsection will return dataPtr by reference!
                __int32 dataLength = FindSubsection(dataPtr, "data", iBytesRead);
                if (dataLength < 0)
                {
                    printm("bad file (4) [no \'data\' sub-section!]");
                    return false;
                }

                // NOTE: Metadata tags will appear at the end of the file,
                // after the data. You will see "LIST". I've also seen
                // "JUNK" and "smpl", etc.

                // Bytes of data following "data"
                ///*** Data must end on an even byte boundary!!! (explains
                // why the file may appear 1 byte over-size for 8-bit mono)
                printm("data-section length (in bytes): " + String(dataLength));

                int AudioFormat = *(__int16*)(&headerPtr[0]);
                if (AudioFormat != 1) // we can't handle compressed WAV-files
                {
                    printm("cannot read this WAV file-type " +
                                                String(AudioFormat) + "!");
                    return false;
                }

                int NumChannels = *(__int16*)(&headerPtr[2]);
                printm("number of channels: " + String(NumChannels));

                int SampleRate = *(__int32*)(&headerPtr[4]);
                printm("sample rate: " + String(SampleRate));

                // BytesPerFrame: Number of bytes per frame
                int BytesPerFrame = *(__int16*)(&headerPtr[12]);
                printm("bytes per frame: " + String(BytesPerFrame));

                int BitsPerWord = *(__int16*)(&headerPtr[14]);
                printm("bits per sample: " + String(BitsPerWord));
                if (BitsPerWord < 8 || BitsPerWord > 64)
                {
                    printm("bits per sample out of range: " + String(BitsPerWord));
                    return false;
                }

                int BytesPerWord = BitsPerWord / 8;
                if (BitsPerWord % 8) // remaining bits?
                    BytesPerWord++; // round up

                if (BytesPerWord > 8)
                {
                    printm("error: can't handle samples over 64-bits!");
                    return false;
                }

                if (NumChannels * BytesPerWord != BytesPerFrame)
                {
                    printm("error: (NumChannels * BytesPerWord != BytesPerFrame)");
                    return false;
                }

                // there should be no "remainder" bytes...
                if (dataLength % (NumChannels * BytesPerWord))
                {
                    printm("error: incomplete data-block!");
                    return false;
                }

                printm("bytes per sample: " + String(BytesPerWord));

                int TotalFrames = dataLength / (NumChannels * BytesPerWord);
                printm("number of frames: " + String(TotalFrames));

                // make sure we have a file-length that accomodates the expected data-length!
                if (dataPtr + dataLength > fileBuf + iBytesRead)
                {
                    printm("error: [dataPtr+dataLength > fileBuf+iBytesRead!]");
                    return false;
                }

                // Need to populate a PSTOR structure for S950
                ps.loopstart = 0; // first replay point for looping (4 bytes)
                ps.endpoint = TotalFrames - 1; // end of play index (4 bytes)
                ps.looplen = ps.endpoint; // loop length (4 bytes)

                UInt32 tempfreq = SampleRate;
                if (tempfreq > 49999)
                    tempfreq = 49999;
                ps.freq = (UInt32)tempfreq; // sample freq. in Hz (4 bytes)

                ps.pitch = 960; // pitch - units = 1/16 semitone (4 bytes) (middle C)
                ps.totalct = TotalFrames; // total number of words in sample (4 bytes)
                ps.period = 1.0e9 / (double)ps.freq; // sample period in nanoseconds (8 bytes)

                // 8-14 bits S900 or 8-16-bits S950
                // (this will be the bits-per-word of the sample residing on the machine)
                // (DO THIS BEFORE SETTING shift_count!)
                ps.bits_per_word =
                    (UInt16)((BitsPerWord > machine_max_bits_per_word) ?
                        machine_max_bits_per_word : BitsPerWord);

                // positive result is the amount of right shift needed (if any)
                // to down-convert the wav's # bits to the desired # bits
                // (example: Wave-file's BitsPerWord is 16 and ps.bits_per_word
                // max is 14 bits for S900, result is "need to right-shift 2")
                // (DO THIS AFTER LIMITING ps.bits_per_word TO machine_max_bits_per_word!)
                int shift_count = BitsPerWord - ps.bits_per_word;

                memmove(ps.name, newname, MAX_NAME_S900); // ASCII sample name (10 bytes)
                ps.name[MAX_NAME_S900] = '\0';
                trimright(ps.name);

                // clear spares
                for (int ii = 0; ii < PSTOR_SPARE_COUNT; ii++)
                    ps.spares[ii] = 0;

                ps.loudnessoffset = 0;

                // set alternating/reverse flags "normal"
                ps.flags = 0;

                ps.loopmode = 'O'; // (1 byte) (one-shot)

                // find the sample's index on the target machine
#if !TESTING
                if ((sampIndex = FindIndex(ps.name)) < 0)
                    return false;
#else
                if (m_use_sample_dump_standard)
                {
                    // Show dialog to obtain a sample number...
                    if (FormChoose->ShowModal() == mrCancel)
                        return;
                    sampIndex = FormChoose->SampleIndex;
                }
                else
                    sampIndex = 0;
#endif
                // encode samp_hedr and samp_parms arrays
                encode_sample_info(sampIndex, &ps);

                print_ps_info(&ps);

               // if Stereo and User checked "send right channel"
               // point to first right-channel sample-datum
                if (NumChannels > 1 && m_use_right_chan)
                    dataPtr += BytesPerWord;

                __int64 wav_baseline = (1 << (BitsPerWord - 1));
                __int64 target_baseline = (1 << (ps.bits_per_word - 1));
                __int64 ob_target_max = (1 << ps.bits_per_word) - 1;
                __int64 signed_target_max = target_baseline - 1;
                __int64 signed_target_min = -signed_target_max;

                __int64 my_max = signed_target_min;
                __int64 my_min = signed_target_max;

                // the largest magnitude (pos or neg) point in this wav
                // (needed to compute a scale-factor to map the wav to
                // the target # bits)
                unsigned __int64 maxAbsAcc = 0;

                // init progress display
                int divisor = (ps.totalct <= 19200) ? 8 : 32;
                String dots;

                bool bUseScaling = (BytesPerWord < 2) ? false : true;
                int passes = bUseScaling ? 2 : 1;

                // Make two passes... first to get max absolute value of the original
                // WAV sample-point to compute a scaling factor to normalize to 14-bits.
                while (passes > 0)
                {
                    blockct = 0;

                    // when passes == 1, that's the "really send the sample" pass...
                    // (as opposed to merely  finding the max for the scale-factor)
                    if (passes == 1)
                    {
                        if (bUseScaling)
                        {
                            double scale_factor = (double)signed_target_max / (double)maxAbsAcc;
                            printm("\r\nscale factor (targ max/file max): " +
                                String(signed_target_max) + "/" + String(maxAbsAcc) + " = " +
                                AnsiString::Format("%.3f\r\n", ARRAYOFCONST((scale_factor))));
                        }

                        // request common reception enable (after 25ms delay)
                        if (!exmit(0, SECRE, true))
                            return false; // tx timeout

                        DelayGpTimer(25);

                        // transmit sample header info
                        if (!comws(m_hedr_size, samp_hedr, false))
                            return false;

                        // wait for acknowledge
                        // (for midi - the data sent is initially buffered by
                        // between 229 and 247 bytes so we CANNOT get
                        // synchronized ack-packets! [Unless it is SDS])
                        if (get_ack(0))
                            return false;
                    }
                    else
                        printm("computing largest absolute sample value (wait...)");

                    int FrameCounter = 0;
                    Byte* dp = dataPtr;

                    // for each frame in WAV-file (one-sample of 1-8 bytes...)
                    for (;;)
                    {
                        // End of file?
                        if (FrameCounter >= TotalFrames)
                            break;

                        // read and encode block of 20, 30 or 60 frames (samples)...

                        // Strategy: encode WAV samples of 8-64 bits
                        // as unsigned 14 (or 16-bit) (two-byte S950 "SW" format)
                        // S900 is 12-bits but can receive 14-bits.
                        // 8-bit wav-format is already un-signed, but
                        // over 8-bits we need to convert from signed to
                        // unsigned.  All values need to be converted into
                        // a 14-bit form for S950...

                        // For both S900 and S950, the data are encoded into
                        // 2 bytes per sample - 60 samples per 120 byte block.

                        // (For the Prophet 2000 data are encoded into 3 bytes per
                        // sample - 40 samples per 120 byte block. And this format
                        // also handles 30 samples encoded 4-bytes * 30 = 120,
                        // but that capability is unrealized for the S900/S950)

                        checksum = initial_checksum; // init checksum

                        if (passes == 1)
                        {
                            // reset transmit buffer pointer
                            tbuf[0] = (Byte)(blockct & 0x7f); // queue the block #
                            ptbuf = &tbuf[1];
                        }

                        register __int64 acc; // sample accumulator (must be signed!)

                        for (int ii = 0; ii < words_per_block; ii++)
                        {
                            if (FrameCounter >= TotalFrames)
                                acc = 0;
                            else
                            {
                                // one-byte wave samples are un-signed by default
                                if (BytesPerWord < 2) // one byte?
                                {
                                    acc = *dp;

                                    if (passes == 2)
                                        if (acc > maxAbsAcc)
                                            maxAbsAcc = acc;
                                }
                                else // between 2-7 bytes per sample (signed)
                                {
                                    // Allowed BitsPerWord => 9-56
                                    // Stored as MSB then LSB in "dp" buffer.
                                    // Left-justified...
                                    //
                                    // From the WAV file:
                                    //
                                    // example (16-bits):
                                    // byte *dp    : D15 D14 D13 D12 D11 D10 D09 D08
                                    // byte *(dp+1): D07 D06 D05 D04 D03 D02 D01 D00
                                    //
                                    // example (9-bits):
                                    // byte *dp    : D08 D07 D06 D05 D04 D03 D02 D01
                                    // byte *(dp+1): D00  0   0   0   0   0   0   0

                                    // init 64-bit acc with -1 (all 1) if msb of
                                    // most-signifigant byte is 1 (negative value)
                                    if (dp[BytesPerWord - 1] & 0x80)
                                        acc = -1; // set all 1s
                                    else
                                        acc = 0;

                                    // load accumulator with 8-64 bit sample
                                    // (Microsoft WAV sample-data are in Intel little-endian
                                    // byte-order. left-channel sample appears first for a
                                    // stereo WAV file, then the right-channel.)
                                    for (int ii = BytesPerWord - 1; ii >= 0; ii--)
                                    {
                                        acc <<= 8; // zero-out new space and shift previous
                                        acc |= dp[ii];
                                    }

                                    // right-justify so we can add baseline
                                    // (the bits in a wav are left-justified
                                    // so we must scoot them to the right!)
                                    //
                                    // NOTE: since acc is signed, the sign-bit
                                    // is preserved during the right-shift!
                                    acc >>= (8 * BytesPerWord) - BitsPerWord;

                                    if (passes == 2)
                                    {
                                        // find our max +/- sample value
                                        if (acc < 0)
                                        {
                                            if (-acc > maxAbsAcc)
                                                maxAbsAcc = -acc;
                                        }
                                        else
                                        {
                                            if (acc > maxAbsAcc)
                                                maxAbsAcc = acc;
                                        }
                                    }
                                    else // here, we have computed a scale_factor!
                                    {
                                        if (bUseScaling)
                                        {
                                            acc *= signed_target_max;
                                            double rem = fmod((double)acc, (double)maxAbsAcc);
                                            acc = (double)acc / (double)maxAbsAcc;

                                            if (rem >= 0.5)
                                                acc++;
                                            else if (rem <= -0.5)
                                                acc--;

                                            // convert from 2's compliment to offset-binary
                                            acc += signed_target_max;

                                            if (acc > my_max)
                                                my_max = acc;
                                            if (acc < my_min)
                                                my_min = acc;
                                        }
                                        else
                                        {
                                            // convert from 2's compliment to offset-binary:
                                            acc += wav_baseline;

                                            // convert down to 14 or 16-bits if over...
                                            if (shift_count > 0)
                                            {
                                                // shift msb of discarded bits to lsb of val
                                                acc >>= shift_count - 1;

                                                bool bRoundUp = acc & 1; // need to round up?

                                                                         // discard msb of discarded bits...
                                                acc >>= 1;

                                                if (bRoundUp && acc != ob_target_max)
                                                    acc++;
                                            }
                                        }
                                    }
                                }
                            }

                            // save sample in 122-byte tbuf
                            if (passes == 1)
                                queue((UInt16)acc, ptbuf, bytes_per_word,
                                    ps.bits_per_word, checksum);

                            dp += BytesPerFrame; // Next frame
                            FrameCounter++;
                        }

                        // user can press ESC to quit
                        Application->ProcessMessages();

                        if (m_abort)
                        {
                            printm("user aborted transmit!");
                            return false;
                        }

                        if (passes == 1)
                        {
                            *ptbuf++ = checksum; // queue checksum

                            if (ptbuf - tbuf != m_data_size)
                                printm("detected tbuf overrun (should never happen!)");

                            // write the data block (no delay)
                            if (!comws(m_data_size, tbuf, false))
                                return false;

                            // do the ..... progress indicator
                            if (blockct % divisor == 0)
                            {
                                dots += ".";
                                printm(dots);
                            }

                            // wait for acknowledge
                            if (get_ack(blockct))
                            {
                                // sample send failed!
                                unsigned countSent = blockct*words_per_block;
                                printm("sent " + String(countSent) + " of " +
                                    String(ps.totalct) + " sample words!");

                                // limits
                                if (countSent < ps.totalct)
                                {
                                    ps.totalct = countSent;
                                    ps.endpoint = ps.totalct - 1;
                                    ps.looplen = ps.endpoint;
                                    ps.loopstart = 0;

                                    // re-encode modified values
                                    encode_sample_info(sampIndex, &ps);
                                    printm("sample length was truncated!");
                                }

                                bSendAborted = true;
                                break;
                            }
                        }

                        blockct++;
                    }

                    // print max and min if computing scale-factor and we have
                    // applied it in this, second (and last) pass - just before
                    // we exit...
                    if (bUseScaling && passes == 1)
                        printm("target_max: " + String(ob_target_max) +
                                    ", scaled-max: " + String(my_max) +
                                    ", scaled-min: " + String(my_min));

                    passes--;

                } // end while
            }
            else // .AKI file (my custom format)
            {
                // File format: (little-endian storage format, LSB then MSB)
                // 1) 4 byte unsigned, magic number to identify type of file
                // 2) 72 byte PSTOR struct with sample's info
                // 3) At offset 20 into PSTOR is the 4-byte number of sample-words
                // 4) X sample words in 16-bit 2's compliment little endian format
                // Offset 32 has a 16-bit int with the # bits per sample-word.

                // min size is one 60-byte block of data plus the magic-number and
                // PSTOR (program info) array

                if (iBytesRead < (int)(DATA_PACKET_SIZE +
                                        AKI_FILE_HEADER_SIZE + UINT32SIZE))
                {
                    printm("file is corrupt");
                    return false;
                }

                printm("Read " + String(iBytesRead) + " bytes");

                UInt32 my_magic;

                memcpy(&my_magic, &fileBuf[0], UINT32SIZE);

                //printm("aki-file magic #: " + String(my_magic));
                if (my_magic != MAGIC_NUM_AKI)
                {
                    printm("File is not the right kind!");
                    return false;
                }

                // load ps (the sample-header info) from fileBuffer
                memcpy(&ps, &fileBuf[0 + UINT32SIZE], AKI_FILE_HEADER_SIZE);
                ps.name[MAX_NAME_S900] = '\0';
                trimright(ps.name);

                // get the +/- shift_count before we limit ps.bits_per_word (below)
                // (should either be 16-16 = 0, 14-14 = 0, 14-16 = -2 or 16-14 = +2
                // the only case we care about is +2 because we will have to
                // down-convert by right-shifting 2 to target an S900 with
                // a 16-bit sample saved from an S950)
                int shift_count = ps.bits_per_word - machine_max_bits_per_word;

                // 2 bytes 14-bits S900 or 3 bytes 16-bits S950
                // If the .aki file has 16-bit samples and we are sending to
                // an S900, we need to lower the PSTOR bits_per_word to 14!
                if (ps.bits_per_word > machine_max_bits_per_word)
                {
                    ps.bits_per_word = (UInt16)machine_max_bits_per_word;
                    printm("reduced bits per word in .aki file to fit the S900 (14-bits max)!");
                    printm("(if you are sending to the S950 select \"Target S950\" in the menu!)");
                }

                // find the sample's index on the target machine
#if !TESTING
                if ((sampIndex = FindIndex(ps.name)) < 0)
                    return false;
#else
                sampIndex = 0;
#endif

                // encode samp_hedr and samp_parms arrays
                encode_sample_info(sampIndex, &ps);

                print_ps_info(&ps);

                // request common reception enable (after 25ms delay)
                if (!exmit(0, SECRE, true))
                    return false; // tx timeout

                DelayGpTimer(25);

                // transmit sample header info
                if (!comws(m_hedr_size, samp_hedr, false))
                    return false;

                // wait for acknowledge
                // (for midi - the data sent is initially buffered by
                // between 229 and 247 bytes so we CANNOT get
                // synchronized ack-packets!)
                if (get_ack(0))
                    return false;

                __int16 baseline = (__int16)(1 << (ps.bits_per_word - 1));

                UInt16 max_val = (UInt16)((1 << ps.bits_per_word) - 1);

                __int16 *ptr = (__int16*)&fileBuf[AKI_FILE_HEADER_SIZE + UINT32SIZE];
                int ReadCounter = AKI_FILE_HEADER_SIZE + UINT32SIZE; // We already processed the header

                blockct = 0;

                // init progress display
                int divisor = (ps.totalct <= 19200) ? 8 : 32;
                String dots;

                for (;;)
                {
                    // End of file?
                    if (ReadCounter >= iBytesRead)
                        break;

                    checksum = initial_checksum; // init checksum

                    // reset transmit buffer pointer
                    tbuf[0] = (Byte)(blockct & 0x7f); // queue the block #
                    ptbuf = &tbuf[1];

                    // Send data and checksum (send 0 if at end-of-file to pad
                    // out the data packet to 120 bytes!)
                    for (int ii = 0; ii < words_per_block; ii++)
                    {
                        // read block of 120 bytes (40 16-bit sample words or
                        // 60 14-bit sample words) from fileBuffer...
                        // pad last block with 0's if end of file
                        __int16 val = (ReadCounter >= iBytesRead) ? (__int16)0 : *ptr++;

                        // convert back to full-range rather than two's compliment
                        val += baseline;

                        ReadCounter += 2;

                        // if targeting the older S900, we have to convert 16-bit samples
                        // to 14-bits, rounding up if needed
                        if (shift_count > 0)
                        {
                            // shift msb of discarded bits to lsb of val
                            val >>= shift_count - 1;

                            bool bRoundUp = val & 1; // need to round up?

                            // discard msb of discarded bits...
                            val >>= 1;

                            if (bRoundUp && val != max_val)
                                val++;

                        }

                        queue(val, ptbuf, bytes_per_word, ps.bits_per_word, checksum);
                    }

                    // user can press ESC to quit
                    Application->ProcessMessages();

                    if (m_abort)
                    {
                        printm("user aborted transmit!");
                        return false;
                    }

                    *ptbuf++ = checksum; // queue checksum

                    if (ptbuf - tbuf != m_data_size)
                        printm("detected tbuf overrun (should never happen!)");

                    // write the data block (no delay)
                    if (!comws(m_data_size, tbuf, false))
                        return false;

                    // do the ..... progress indicator
                    if (blockct % divisor == 0)
                    {
                        dots += ".";
                        printm(dots);
                    }

                    // wait for acknowledge
                    if (get_ack(blockct))
                    {
                        // sample send failed!
                        unsigned countSent = blockct*words_per_block;
                        printm("sent " + String(countSent) + " of " +
                            String(ps.totalct) + " sample words!");

                        // limits
                        if (countSent < ps.totalct)
                        {
                            ps.totalct = countSent;
                            if (ps.endpoint > ps.totalct - 1)
                                ps.endpoint = ps.totalct - 1;
                            if (ps.looplen > ps.endpoint)
                                ps.looplen = ps.endpoint;
                            if (ps.loopstart > ps.endpoint - ps.looplen)
                                ps.loopstart = ps.endpoint - ps.looplen;

                            // re-encode modified values
                            encode_sample_info(sampIndex, &ps);
                            printm("sample length was truncated!");
                        }

                        bSendAborted = true;
                        break;
                    }

                    blockct++;
                }
            }

            // write the EEX (no delay)
            Byte temp = EEX;
            if (!comws(1, &temp, false))
                return false;

#if !TESTING
            // do renaming unless NAK packet was received while in midi Tx
            // (in that case we have no way of knowing exactly which packet
            // failed so we can't change samp_parms correctly... oh well...)
            if (m_auto_rename && !bSendAborted)
            {
                // look up new sample in catalog, when you write a new sample it
                // shows up as "00", "01", "02"

                Byte locstr[3];
                sprintf(locstr, "%02d", sampIndex);

                sampIndex = findidx(locstr);

                // returns the 0-based sample index if a match is found
                // -1 = error
                // -2 = no samples on machine
                // -3 = samples on machine, but no match
                if (sampIndex == -1)
                {
                    printm("catalog search error for: \"" + String((char*)locstr) + "\"");
                    return false; // catalog error
                }

                if (sampIndex == -2)
                    sampIndex = 0; // we will be the only sample...

                if (sampIndex < 0)
                {
                    printm("index string \"" + String((char*)locstr) + "\" not found!");
                    return false;
                }

                send_samp_parms(sampIndex); // write samp_parms to midi or com port

                if (!bSendAborted)
                    printm("sample written ok! (index=" + String(sampIndex) + ")");
            }
            else
            {
                if (!bSendAborted)
                    printm("sample written ok!");
            }
#else
            if (!bSendAborted)
                printm("sample written ok!");
#endif
        }
        catch (...)
        {
            printm("unable to process file: \"" + sFilePath + "\"");
            return false;
        }
    }
    __finally
    {
        // request common reception disable (after 25ms delay)
        exmit(0, SECRD, true);

        if (fileBuf)
            delete[] fileBuf;

        if (tbuf)
            delete[] tbuf;

        if (iFileHandle != 0)
            FileClose(iFileHandle);
    }

    return true;
}
//---------------------------------------------------------------------------
// prev = dcRemoval(signal, prev.w, 0.9);
//double __fastcall TFormMain::dcRemoval(double input, double &prevHistory, double alpha)
//{
//    double newHistory = input + alpha*prevHistory;
//    double output = newHistory - prevHistory;
//    prevHistory = newHistory; // return by-reference
//    return output;
//}
//---------------------------------------------------------------------------
// tricky algorithm I came up with (the inverse of the one in RxSamp.cpp)
// builds and sends only the required number of 7-bit bytes representing one
// sample-word to the S900/S950 - S.S.
//
// checksum in/out is by-reference as is the ptbuf pointer
void __fastcall TFormMain::queue(UInt16 val, Byte* &ptbuf,
                 int bytes_per_word, int bits_per_word, Byte &checksum)
{
    for (int ii = 1; ii <= bytes_per_word; ii++)
    {
        int shift_count = bits_per_word - (ii * 7);

        Byte out_val;

        if (shift_count >= 0)
            out_val = (Byte)(val >> shift_count);
        else
            out_val = (Byte)(val << -shift_count);

        out_val &= 0x7f; // mask msb to 0

        // update reference in/out vars
        checksum ^= out_val;
        *ptbuf++ = out_val;
    }
}
//---------------------------------------------------------------------------
// puts PSTOR struct (ps) info into samp_parms[] and samp_hedr[] arrays
void __fastcall TFormMain::encode_sample_info(UInt16 samp, PSTOR* ps)
{
    //
    // SAMPLE PARAMETERS 129 bytes (do this before encoding header)
    //

    samp_parms[0] = BEX;
    samp_parms[1] = AKAI_ID;
    samp_parms[2] = 0; // midi chan
    samp_parms[3] = SPRM;
    samp_parms[4] = S900_ID;
    samp_parms[5] = (Byte)(samp & 0x7f);
    samp_parms[6] = (Byte)((samp>>7) & 0x7f); // reserved

    // copy ASCII sample name, pad with blanks and terminate
    Byte locstr[MAX_NAME_S900 + 1];
    memmove(locstr, ps->name, MAX_NAME_S900);
    int size = strlen(locstr);
    while (size < MAX_NAME_S900)
        locstr[size++] = ' ';
    locstr[MAX_NAME_S900] = '\0';

    encode_parmsDB((Byte*)locstr, &samp_parms[7], MAX_NAME_S900); // 20

                                                                           // clear unused bytes
                                                                           // 27-34  DD Undefined
                                                                           // 35-38  DW Undefined
    for (int ii = 27; ii <= 38; ii++)
        samp_parms[ii] = 0;

    // number of sample words
    encode_parmsDD(ps->totalct, &samp_parms[39]); // 8

                                                  // sampling frequency
    encode_parmsDW(ps->freq, &samp_parms[47]); // 4

                                               // pitch
    encode_parmsDW(ps->pitch, &samp_parms[51]); // 4

                                                // loudness offset
    encode_parmsDW(ps->loudnessoffset, &samp_parms[55]); // 4

                                                         // replay mode 'A', 'L' or 'O' (alternating, looping or one-shot)
                                                         // (samp_hedr[18] also has sample's loop mode: 0=looping, 1=alternating
                                                         // or one-shot if loop-length < 5)
    encode_parmsDB(ps->loopmode, &samp_parms[59]); // 2

    encode_parmsDB(0, &samp_parms[61]); // reserved 2

                                        // end point relative to start of sample
    encode_parmsDD(ps->endpoint, &samp_parms[63]); // 8

                                                   // loop start point
    encode_parmsDD(ps->loopstart, &samp_parms[71]); // 8

                                                    // limit loop length (needed below when setting samp_hedr[15])
    if (ps->looplen > ps->endpoint)
        ps->looplen = ps->endpoint;

    // length of looping or alternating part (default is 45 for TONE sample)
    encode_parmsDD(ps->looplen, &samp_parms[79]); // 8

    encode_parmsDW(0, &samp_parms[87]); // reserved 4

                                        // 91,92 DB type of sample 0=normal, 255=velocity crossfade
    Byte c_temp = (Byte)((ps->flags & (Byte)1) ? 255 : 0);
    encode_parmsDB(c_temp, &samp_parms[91]);

    // 93,94 DB waveform type 'N'=normal, 'R'=reversed
    c_temp = (ps->flags & (Byte)2) ? 'R' : 'N';
    encode_parmsDB(c_temp, &samp_parms[93]); // 2

    // clear unused bytes
    // 95-126  4 DDs Undefined 32
    for (int ii = 95; ii <= 126; ii++)
        samp_parms[ii] = 0;

    // checksum is exclusive or of 7-126 (120 bytes)
    // and the value is put in index 127
    compute_checksum(7, 127);

    samp_parms[128] = EEX;

    //
    // SAMPLE HEADER, 19 bytes (21 bytes for sample-dump-standard (SDS))
    //

    // encode excl, syscomid, sampdump
    samp_hedr[0] = BEX;
    samp_hedr[1] = SYSTEM_COMMON_NONREALTIME_ID;
    int idx = 2;
    samp_hedr[idx++] = SD;
    samp_hedr[idx++] = (Byte)(samp & 0x7f);
    samp_hedr[idx++] = (Byte)((samp>>7) & 0x7f); // MSB samp idx always 0 for S900

    // bits per word
    samp_hedr[idx++] = (Byte)ps->bits_per_word;

    // sampling period
    encode_hedrTB(ps->period, &samp_hedr[idx]); // 3
    idx += 3;

    // number of sample words
    encode_hedrTB(ps->totalct, &samp_hedr[idx]); // 3
    idx += 3;

    // loop start point
    encode_hedrTB(ps->endpoint - ps->looplen, &samp_hedr[idx]); // 3
    idx += 3;

    // loop end, S950 takes this as the end point
    encode_hedrTB(ps->endpoint, &samp_hedr[idx]); // 3
    idx += 3;

    // use ps->looping mode 'A', 'L' or 'O' (alternating, looping or one-shot)
    // to set samp_hedr[18], loop mode: 0=looping, 1=alternating
    samp_hedr[idx] = (Byte)((ps->loopmode == 'A') ? 1 : 0);
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::send_samp_parms(unsigned index)
{
    samp_parms[5] = (Byte)(index);

    // transmit sample parameters (after 25ms delay)
    if (!comws(PARMSIZ, samp_parms, true))
        return;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::encode_parmsDB(Byte c, Byte* dest)
{
    *dest++ = (c & (Byte)0x7f);
    *dest = (Byte)((c & (Byte)0x80) ? 1 : 0);
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::encode_parmsDB(Byte* source, Byte* dest, int numchars)
{
    for (int ii = 0; ii < numchars; ii++)
    {
        *dest++ = (*source & (Byte)0x7f);
        *dest++ = (Byte)((*source & (Byte)0x80) ? 1 : 0);
        source++;
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::encode_parmsDD(UInt32 value, Byte* tp)
{
    for (int ii = 0; ii < 4; ii++)
    {
        *tp++ = (Byte)(value & 0x7f);
        value >>= 7;
        *tp++ = (Byte)(value & 1);
        value >>= 1;
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::encode_parmsDW(UInt32 value, Byte* tp)
{
    for (int ii = 0; ii < 2; ii++)
    {
        *tp++ = (Byte)(value & 0x7f);
        value >>= 7;
        *tp++ = (Byte)(value & 1);
        value >>= 1;
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::encode_hedrTB(UInt32 value, Byte* tp)
{
    *tp++ = (Byte)(value & 0x7f);
    value >>= 7;
    *tp++ = (Byte)(value & 0x7f);
    value >>= 7;
    *tp = (Byte)(value & 0x7f);
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::compute_checksum(int min_index, int max_index)
{
    Byte checksum;
    int ii;

    // checksum and store in transmission array
    checksum = 0;

    for (ii = min_index; ii < max_index; ii++)
        checksum ^= samp_parms[ii];

    samp_parms[max_index] = checksum; // offset 126 is checksum
}
//---------------------------------------------------------------------------
// Catalog routines
//---------------------------------------------------------------------------
bool __fastcall TFormMain::catalog(bool print)
{
    // 7 hedr bytes + chksum byte + EEX = 9...
    if (m_rxByteCount < 9 || TempArray[3] != DCAT)
        return false;

    int entries = (m_rxByteCount - 9) / sizeof(S950CAT);

    if (!entries)
    {
        printm("No Samples or programs in S950");
        return true;
    }

    S950CAT *tempptr = (S950CAT *)&TempArray[7]; // Skip header
    CAT *permsampptr = PermSampArray;
    CAT *permprogptr = PermProgArray;

    m_numSampEntries = 0;
    m_numProgEntries = 0;

    for (int ii = 0; ii < entries; ii++)
    {
        if (tempptr->type == 'S')
        {
            if (m_numSampEntries < MAX_SAMPS)
            {
                sprintf(permsampptr->name, "%.*s", MAX_NAME_S900, tempptr->name);
                trimright(permsampptr->name);

                permsampptr->index = tempptr->index;

                m_numSampEntries++; // increment counter
                permsampptr++; // next structure
            }
        }
        else if (tempptr->type == 'P')
        {
            if (m_numProgEntries < MAX_PROGS)
            {
                sprintf(permprogptr->name, "%.*s", MAX_NAME_S900, tempptr->name);
                trimright(permprogptr->name);

                permprogptr->index = tempptr->index;

                m_numProgEntries++; // increment counter
                permprogptr++; // next structure
            }
        }

        tempptr++; // next structure
    }

    if (print)
    {
        printm("Programs:");

        for (int ii = 0; ii < m_numProgEntries; ii++)
            printm(String(PermProgArray[ii].index) + ":\"" +
                String(PermProgArray[ii].name) + "\"");

        printm("Samples:");

        for (int ii = 0; ii < m_numSampEntries; ii++)
            printm(String(PermSampArray[ii].index) + ":\"" +
                String(PermSampArray[ii].name) + "\"");
    }

    return true;
}
//---------------------------------------------------------------------------
// Receive data routines
//---------------------------------------------------------------------------
int __fastcall TFormMain::receive(int count, bool displayHex)
// set count 0 to receive a complete message (EEX required)
// set count -1 to receive a partial message of unknown size
// set "count" to return either when a specific # bytes is received
//  or when EEX is received
//
// returns 0 if success, 1 if failure, 2 if buffer overrun
{
    Byte tempc;
    bool have_bex = false;

    m_rxByteCount = 0;

    m_rxTimeout = false;
    m_abort = false;
    m_inBufferFull = false;

    Timer1->Enabled = false; // stop timer (just in-case!)
    Timer1->Interval = RXTIMEOUT; // 3.1 seconds
    Timer1->OnTimer = Timer1RxTimeout; // set handler
    Timer1->Enabled = true; // start timeout timer

    try
    {
        for (;;)
        {
            if (m_abort)
            {
                printm("receive aborted by user!");
                break;
            }

            if (ApdComPort1->CharReady())
            {
                if (m_rxByteCount >= TEMPARRAYSIZ) // at buffer capacity... error
                {
                    m_inBufferFull = true;
                    return 2;
                }

                // keep ressetting timer to hold off timeout
                Timer1->Enabled = false; // stop timer (must do!)
                Timer1->Interval = RXTIMEOUT; // 3.1 seconds
                Timer1->Enabled = true;

                tempc = ApdComPort1->GetChar();

                if (!have_bex)
                {
                    if (tempc == BEX || count != 0)
                    {
                        have_bex = true;
                        TempArray[m_rxByteCount++] = tempc;
                    }
                }
                else
                {
                    TempArray[m_rxByteCount++] = tempc;

                    if (tempc == EEX ||
                            (count > 0 && m_rxByteCount >= (UInt32)count))
                        return 0;
                }
            }
            else
            {
                Application->ProcessMessages(); // need this to detect the timeout

                if (m_rxTimeout)
                {
                    // return whatever we have if count == -1
                    if (count == -1 && m_rxByteCount > 0)
                        return 0;

                    break;
                }
            }
        }
    }
    __finally
    {
        Timer1->Enabled = false;
        Timer1->OnTimer = NULL; // clear handler
    }

    return 1; // timeout
}
//---------------------------------------------------------------------------
// Misc methods
//---------------------------------------------------------------------------
void __fastcall TFormMain::printm(String message)
{
    Memo1->Lines->Add(message);
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::print_ps_info(PSTOR* ps)
{
    printm("");
    printm("Output Parameters:");
    printm("sample length (in words): " + String(ps->totalct));
    printm("end point: " + String(ps->endpoint));
    printm("frequency (hertz): " + String(ps->freq));
    printm("pitch: " + String(ps->pitch));
    printm("period (nanoseconds): " + String((UInt32)ps->period));
    printm("bits per word: " + String(ps->bits_per_word));
    printm("loop start point: " + String(ps->loopstart));
    printm("loop length: " + String(ps->looplen));

    if (ps->flags & (Byte)1)
        printm("velocity crossfade: on");
    else
        printm("velocity crossfade: off");

    if (ps->flags & (Byte)2)
        printm("reverse waveform: yes");
    else
        printm("reverse waveform: no");

    if (ps->loopmode == 'O')
        printm("looping mode: one-shot");
    else if (ps->loopmode == 'L')
        printm("looping mode: looping");
    else if (ps->loopmode == 'A')
        printm("looping mode: alternating");
    else
        printm("looping mode: unknown");

    printm("sample name: \"" + String(ps->name) + "\""); // show any spaces
}
//---------------------------------------------------------------------------
int __fastcall TFormMain::FindIndex(Byte* pName)
{
    // now we need to set "samp index"
    int sampIndex = findidx(pName);

    // retval = the 0-based sample index if a match is found
    // -1 = error
    // -2 = no samples on machine
    // -3 = samples on machine, but no match
    if (sampIndex == -1)
    {
        printm("catalog search error for: \"" + String((char*)pName).TrimRight() + "\"");
        printm("check RS232 connection and/or baud-rate: " + String(m_baud));
    }
    else if (sampIndex >= 0) // sample we are about to write is already on machine
        printm("found sample \"" + String((char*)pName).TrimRight() + "\" at index " + String(sampIndex));
    else if (sampIndex == -3) // samples on machine, but not the one we are about to write
        sampIndex = m_numSampEntries;
    else // no samples on machine
        sampIndex = 0;

    return sampIndex;
}
//---------------------------------------------------------------------------
int __fastcall TFormMain::findidx(Byte* sampName)
// returns the 0-based sample index if a match is found
// -1 = error
// -2 = no samples on machine
// -3 = samples on machine, but no match
//
// ignores spaces to right of name in comparison
{
    try
    {
        // Request S950 Catalog (after 25ms delay)
        if (!exmit(0, RCAT, true))
            return -1; // tx timeout

        if (receive(0))
            return -1; // rx timeout

        // sets m_numSampEntries...
        // NOTE: the program/sample names are all right-trimmed by catalog().
        if (!catalog(false)) // populate sample and program structs (no printout)
            return -1;

        if (m_numSampEntries == 0)
            return -2; // no samples on machine

        // make a right-trimmed copy of name
        Byte newName[MAX_NAME_S900 + 1];
        memmove(newName, sampName, MAX_NAME_S900);
        newName[MAX_NAME_S900] = '\0';
        trimright(newName);

        for (int ii = 0; ii < m_numSampEntries; ii++)
            if (bytewisecompare(PermSampArray[ii].name, newName, MAX_NAME_S900)) // names match?
                return PermSampArray[ii].index; // found match!
    }
    catch(...)
    {
        return -1; // exception
    }

    return -3; // samples on machine, but no match
}
//---------------------------------------------------------------------------
// return true if match
bool __fastcall TFormMain::bytewisecompare(Byte* buf1, Byte* buf2, int maxLen)
{
    for (int ii = 0; ii < maxLen; ii++)
    {
        if (buf1[ii] != buf2[ii])
            return false; // not the same!

        if (buf1[ii] == 0)
            break;
    }

    return true; // byte-wise compare the same!
}
//---------------------------------------------------------------------------
String __fastcall TFormMain::GetFileName(void)
{
    OpenDialog1->Title = "Send .wav or .aki file to S900/S950";
    OpenDialog1->DefaultExt = "aki";
    OpenDialog1->Filter = "Aki files (*.aki)|*.aki|" "Wav files (*.wav)|*.wav|"
        "All files (*.*)|*.*";
    OpenDialog1->FilterIndex = 3; // start the dialog showing all files
    OpenDialog1->Options.Clear();
    OpenDialog1->Options << ofHideReadOnly
        << ofPathMustExist << ofFileMustExist << ofEnableSizing;

    if (!OpenDialog1->Execute())
        return ""; // Cancel

    String sFileName = OpenDialog1->FileName;

    return sFileName;
}
//---------------------------------------------------------------------------
// Search file for named chunk
//
// Returns:
// -2 = not found
// -1 = exception thrown
// 0-N length of chunk
//
// Returns a byte-pointer to the start of data in the chunk
// as a reference: fileBuffer
//
// On entry, set fileBuffer to the start of the file-buffer
__int32 __fastcall TFormMain::FindSubsection(Byte* &fileBuffer, char* chunkName, UINT fileLength)
{
    try
    {
        // bypass the first 12-bytes "RIFFnnnnWAVE" at the file's beginning...
        Byte* chunkPtr = fileBuffer + 12;

        Byte* pMax = fileBuffer + fileLength;

        int chunkLength;

        // Search file for named chunk
        for (;;)
        {
            // Chunks are 4-bytes ANSI followed by a 4-byte length, lsb-to-msb
            if (chunkPtr + 8 >= pMax)
                return -2;

            chunkLength = *(__int32*)(chunkPtr + 4);

            // look for the 4-byte chunk-name
            if (StrCmpCaseInsens((char*)chunkPtr, (char*)chunkName, 4))
            {
                fileBuffer = chunkPtr + 8; // return pointer to data, by reference
                return chunkLength;
            }

            chunkPtr += chunkLength + 8;
        }
    }
    catch (...)
    {
        return -1;
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::MenuPutSampleClick(TObject *Sender)
{
    String filePath = GetFileName();

    if (!filePath.IsEmpty())
        PutSample(filePath);

    m_DragDropFilePath = "";
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::MenuGetCatalogClick(TObject *Sender)
{
    ListBox1->Clear();
    Memo1->Clear();

    try
    {
        // Request S950 Catalog (no delay)
        if (!exmit(0, RCAT, false))
            return;

        if (receive(0))
        {
            printm("timeout receiving catalog!");
            return;
        }
    }
    __finally
    {
    }

    if (!catalog(true))
    {
        printm("incorrect catalog received!");
        return;
    }

    return;
}
//---------------------------------------------------------------------------
// This fills the list-box (at the far left) with sample-names, you then
// click a name to receive that particular sample from the Akai.
void __fastcall TFormMain::MenuGetSampleClick(TObject *Sender)
{
    ListBox1->Clear();
    Memo1->Clear();

    try
    {
        try
        {
            // Request S950 Catalog (no delay)
            if (!exmit(0, RCAT, false))
                return;

            if (receive(0))
            {
                printm("timeout receiving catalog!");
                return;
            }

            if (!catalog(false)) // sets m_numSampEntries
            {
                printm("incorrect catalog received!");
                return;
            }

            if (!m_numSampEntries)
            {
                printm("no samples in machine!");
                return;
            }

            // (we save the actual sample index in the object-field...)
            for (int ii = 0; ii < m_numSampEntries; ii++)
                ListBox1->Items->AddObject(PermSampArray[ii].name,
                                        (TObject*)PermSampArray[ii].index);

            printm("\r\n<--- ***Click a sample at the left to receive\r\n"
                "and save it to a .AKI file***");
        }
        catch (...)
        {
            ShowMessage("Can't get catalog");
        }
    }
    __finally
    {
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::ListBox1Click(TObject *Sender)
{
    String sFile = ListBox1->Items->Strings[ListBox1->ItemIndex];
    if (DoSaveDialog(sFile)) // get new, complete file-path back in sFile...
    {
        ListBox1->Repaint();

        // we stored the actual index from the catalog in the object-field
        int index = (int)ListBox1->Items->Objects[ListBox1->ItemIndex];

        if (GetSample(index, sFile))
            printm("not able to save sample!");
        else
            printm("sample saved as: \"" + sFile + "\"");
    }
}
//---------------------------------------------------------------------------
// pass in sName of a simple file-name like "MyFile1"
// returns, by-reference, the full file-path
bool __fastcall TFormMain::DoSaveDialog(String &sName)
{
    try
    {
        SaveDialog1->Title = "Save a sample as .aki file...";
        SaveDialog1->DefaultExt = "aki";
        SaveDialog1->Filter = "Akai files (*.aki)|*.aki|"
            "All files (*.*)|*.*";
        SaveDialog1->FilterIndex = 2; // start the dialog showing all files
        SaveDialog1->Options.Clear();
        SaveDialog1->Options << ofHideReadOnly
            << ofPathMustExist << ofOverwritePrompt << ofEnableSizing
            << ofNoReadOnlyReturn;

        // Use the sample-name in the list as the file-name
        SaveDialog1->FileName = sName.TrimRight() + ".aki";

        if (SaveDialog1->Execute())
        {
            // Assign the full file name to reference var sName!)
            sName = SaveDialog1->FileName;

            return true;
        }
    }
    catch (...)
    {
        printm("error, can't save file: \"" + SaveDialog1->FileName + "\"");
    }
    sName = "";
    return false;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::MenuHelpClick(TObject *Sender)
{
    String hText = "Connect a cable between the S950's (or S900) RS232\r\n"
        "PORT and COM1 (or other port) on your computer. This is an\r\n"
        "ordinary DB25-male to DB9-female null-modem cable. If your\r\n"
        "computer has no connector, you will need a USB-to-RS232\r\n"
        "adaptor.\r\n\r\n"
        "On the S950/S900 push the MIDI button.\r\n"
        "Push the DOWN button and scroll to menu 5.\r\n"
        "Push the RIGHT button and select \"2\" control by RS232.\r\n"
        "Push the RIGHT button again and enter \"3840\".\r\n"
        "This will set the machine to 38400 baud.\r\n\r\n"
        "To test, select \"Get list of samples and programs\" from\r\n"
        "the menu. A box may appear asking for the com port. Select\r\n"
        "a port and click OK. You should see the samples and programs\r\n"
        "listed in this window.";
    Memo1->Lines->Clear();
    Memo1->Lines->Add(hText + " - Cheers, Scott Swift dxzl@live.com");
//    ShowMessage(hText + " - Cheers, Scott Swift dxzl@live.com");
//    Clipboard()->AsText = hText;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::SetMenuItems(void)
{
    m_data_size = DATA_PACKET_SIZE + DATA_PACKET_OVERHEAD;
    m_hedr_size = HEDRSIZ;
    MenuGetCatalog->Enabled = true;
    MenuGetPrograms->Enabled = true;
    MenuPutPrograms->Enabled = true;
    MenuAutomaticallyRenameSample->Enabled = true;
    ListBox1->Enabled = true;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::MenuUseRightChanForStereoSamplesClick(TObject *Sender)
{
    MenuUseRightChanForStereoSamples->Checked = !MenuUseRightChanForStereoSamples->Checked;
    m_use_right_chan = MenuUseRightChanForStereoSamples->Checked;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::MenuAutomaticallyRenameSampleClick(
    TObject *Sender)
{
    MenuAutomaticallyRenameSample->Checked = !MenuAutomaticallyRenameSample->Checked;
    m_auto_rename = MenuAutomaticallyRenameSample->Checked;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::MenuUseHWFlowControlBelow50000BaudClick(
    TObject *Sender)
{
    MenuUseHWFlowControlBelow50000Baud->Checked =
        !MenuUseHWFlowControlBelow50000Baud->Checked;
    m_force_hwflow = MenuUseHWFlowControlBelow50000Baud->Checked;

    // reset port if below 50000
    if (m_baud < 50000)
        SetComPort(m_baud);
}
//---------------------------------------------------------------------------
// custom .prg file-format:
// magic number 639681490             4 byte __int32
// number of programs                 4 byte __int32
//
// next is a variable number of programs:
// number of bytes in program N       4 byte __int32
// program N data including BEX, checksum and EEX
// (each program has a 83 byte header including BEX, then a variable number of
// keygroups, then a checksum byte and an EEX)
// (each keygroup is 140 bytes)
// (the number of keygroups in a program is a DB at header index 53, 54)
//
// this file's size in bytes          4 byte __int32
void __fastcall TFormMain::MenuGetProgramsClick(TObject *Sender)
{
    ListBox1->Clear();
    Memo1->Clear();

    SaveDialog1->Title = "Save all programs to .pgm file...";
    SaveDialog1->DefaultExt = "pgm";
    SaveDialog1->Filter = "Programs (*.pgm)|*.pgm|"
        "All files (*.*)|*.*";
    SaveDialog1->FilterIndex = 2; // start the dialog showing all files
    SaveDialog1->Options.Clear();
    SaveDialog1->Options << ofHideReadOnly
        << ofPathMustExist << ofOverwritePrompt << ofEnableSizing
        << ofNoReadOnlyReturn;

    SaveDialog1->FileName = "akai_progs.prg";

    if (!SaveDialog1->Execute())
        return;

    GetPrograms(SaveDialog1->FileName.TrimRight());
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::GetPrograms(String sFilePath)
{
    int iFileHandle;

    if (FileExists(sFilePath) == 0)
        iFileHandle = FileCreate(sFilePath);
    else
        iFileHandle = FileOpen(sFilePath, fmShareDenyNone | fmOpenReadWrite);

    if (iFileHandle == 0)
    {
        printm("can't open file to write: \"" + sFilePath + "\"");
        return;
    }

    bool bError = false;
    m_numProgEntries = 0; // need this in __finally

    try
    {
        try
        {
            // Request S950 Catalog (no delay)
            if (!exmit(0, RCAT, false))
                return;

            // TEMPARRAYSIZ must be >= largest program with up to 64 keygroups
            // a single program has PRG_FILE_HEADER_SIZE + (X*PROGKEYGROUPSIZ) + 2 for
            // checksum and EEX bytes
            if (receive(0))
            {
                printm("timeout receiving catalog!");
                bError = true;
                return;
            }

            if (!catalog(false)) // sets m_numProgEntries
            {
                printm("incorrect catalog received!");
                return;
            }

            if (!m_numProgEntries)
            {
                printm("no programs in machine!");
                bError = true;
                return;
            }

            printm("reading " + String(m_numProgEntries) +
                                            " programs from S950/S900...");

            __int32 totalBytesWritten = 0;
            __int32 bytesWritten;

            // write the magic number
            UInt32 my_magic = MAGIC_NUM_PRG; // uniquely identifies a .prg file
            totalBytesWritten += UINT32SIZE;
            bytesWritten = FileWrite(iFileHandle, &my_magic, UINT32SIZE);

            if (bytesWritten != UINT32SIZE)
            {
                printm("problem writing to file...");
                bError = true;
                return;
            }

            // write the number of programs
            totalBytesWritten += UINT32SIZE;
            bytesWritten = FileWrite(iFileHandle, &m_numProgEntries, UINT32SIZE);

            if (bytesWritten != UINT32SIZE)
            {
                printm("problem writing to file...");
                bError = true;
                return;
            }

            String dots;

            // request each program and add it to file on the fly
            for (int ii = 0; ii < m_numProgEntries; ii++)
            {
                int progIndex = PermProgArray[ii].index;

                // request next program and its keygroups (after 25ms delay)
                // (clears m_rxByteCount before PutLong, if midi)
                if (!exmit(progIndex, RPRGM, true))
                    return;

                // receive entire program into TempArray
                if (receive(0))
                {
                    printm("timeout while receiving program!");
                    bError = true;
                    return;
                }

                // size of this program
                __int32 progSize = m_rxByteCount;

                if (progSize < PRG_FILE_HEADER_SIZE + PROGKEYGROUPSIZ + 2)
                {
                    printm("insufficient bytes received: " + String(progSize));
                    bError = true;
                    return;
                }

                if (TempArray[3] != PRGM || TempArray[4] != S900_ID)
                {
                    printm("invalid programs header! (1)");
                    bError = true;
                    return;
                }

                if (TempArray[5] != progIndex)
                {
                    TempArray[5] = progIndex;
// this happens all the time - the machine always sends 0 for program #
// so we just have to write-in a value that will work when we send this
// file back to the machine...
//                    printm("forcing offset 5 (program index) to be " +
//                                        String(progIndex) + "\r\n" +
//                    "(received index was " + String((int)TempArray[5]) + ")");
                }

                if (TempArray[progSize-1] != EEX)
                {
                    printm("expected EEX, got: " + String((int)TempArray[progSize-1]));
                    bError = true;
                    return;
                }

                // compute checksum
                Byte checksum = 0;
                for (int jj = 7; jj < progSize - 2; jj++)
                    checksum ^= TempArray[jj];

                if (TempArray[progSize - 2] != checksum)
                {
                    printm("bad checksum for program " + String(ii) + "!");
                    bError = true;
                    return;
                }

                // get number of keygroups in one program (1-31)
                int numKeygroups = decode_parmsDB(&TempArray[53]); // 53, 54
                if (numKeygroups == 1)
                    printm("program " + String(ii) + " has " +
                                    String(numKeygroups) + " keygroup...");
                else
                    printm("program " + String(ii) + " has " +
                                    String(numKeygroups) + " keygroups...");

                // write program-size (including BEX, checksum and EEX) to file.
                // (this helps when we read a .prg file back, to seperate
                // the individual programs, buffer them and transmit to the
                // machine)
                totalBytesWritten += UINT32SIZE;
                bytesWritten = FileWrite(iFileHandle, &progSize, UINT32SIZE);

                if (bytesWritten != UINT32SIZE)
                {
                    printm("problem writing to file...");
                    bError = true;
                    return;
                }

                // copy program in TempArray to file
                totalBytesWritten += progSize;
                bytesWritten = FileWrite(iFileHandle, TempArray, progSize);

                if (bytesWritten != progSize)
                {
                    printm("problem writing to file...");
                    bError = true;
                    return;
                }

                dots += '.';
                printm(dots);
            }

            // lastly, write the file's total size
            totalBytesWritten += UINT32SIZE;
            bytesWritten = FileWrite(iFileHandle, &totalBytesWritten, UINT32SIZE);

            if (bytesWritten != UINT32SIZE)
            {
                printm("problem writing to file...");
                bError = true;
                return;
            }
        }
        catch (...)
        {
            printm("exception thrown while receiving programs!");
            bError = true;
        }
    }
    __finally
    {
        if (iFileHandle)
            FileClose(iFileHandle);

        if (!bError)
            printm(String(m_numProgEntries) + " programs successfully saved!");
        else
        {
            try { DeleteFile(sFilePath); }
            catch (...) {}
            printm("unable to save programs...");
        }
    }
}
//---------------------------------------------------------------------------
// custom .prg file-format:
// magic number 639681490             4 byte __int32
// number of programs                 4 byte __int32
//
// next is a variable number of programs:
// number of bytes in program N       4 byte __int32
// program N data including BEX, checksum and EEX
// (each program has a 83 byte header including BEX, then a variable number of
// keygroups, then a checksum byte and an EEX)
// (each keygroup is 140 bytes)
// (the number of keygroups in a program is a DB at header index 53, 54)
//
// this file's size in bytes          4 byte __int32
void __fastcall TFormMain::MenuPutProgramsClick(TObject *Sender)
{
    Memo1->Clear();

    OpenDialog1->Title = "Send all programs (.prg file) to Akai...";
    OpenDialog1->DefaultExt = "prg";
    OpenDialog1->Filter = "Programs files (*.prg)|*.prg|"
        "All files (*.*)|*.*";
    OpenDialog1->FilterIndex = 2; // start the dialog showing all files
    OpenDialog1->Options.Clear();
    OpenDialog1->Options << ofHideReadOnly
        << ofPathMustExist << ofFileMustExist << ofEnableSizing;

    if (!OpenDialog1->Execute())
        return; // Cancel

    PutPrograms(OpenDialog1->FileName.TrimRight());
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::PutPrograms(String sFilePath)
{
    int iFileHandle = FileOpen(sFilePath, fmShareDenyNone | fmOpenRead);

    if (iFileHandle == 0)
    {
        printm("can't open file to read: \"" + sFilePath + "\"");
        return;
    }

    bool bError = false;
    Byte* buf = NULL;
    UInt32 numProgs = 0; // need this in __finally

    try
    {
        try
        {
            // one way transfer, no incoming handshake
            ApdComPort1->FlushOutBuffer();

            UInt32 iFileLength = FileSeek(iFileHandle, 0, 2); // seek to end

            // seek/read the stored file-length (__int32 at end of the file)
            FileSeek(iFileHandle, (int)(iFileLength - UINT32SIZE), 0);
            UInt32 storedFileLength;
            UInt32 bytesRead = FileRead(iFileHandle, &storedFileLength, UINT32SIZE);

            if (bytesRead != UINT32SIZE)
            {
                printm("file is corrupt or not the right kind...");
                bError = true;
                return;
            }

            if (storedFileLength != iFileLength)
            {
                printm("file is either corrupt or not a valid .prg file!");
                bError = true;
                return;
            }

            FileSeek(iFileHandle, 0, 0); // back to start

                                         // read the magic number
            UInt32 my_magic; // uniquely identifies a .prg file
            bytesRead = FileRead(iFileHandle, &my_magic, UINT32SIZE);

            if (bytesRead != UINT32SIZE)
            {
                printm("file is corrupt or not the right kind...");
                bError = true;
                return;
            }

            if (my_magic != MAGIC_NUM_PRG)
            {
                printm("file is not a .prg programs file for the S950/S900!");
                bError = true;
                return;
            }

            // read the number of programs
            bytesRead = FileRead(iFileHandle, &numProgs, UINT32SIZE);

            if (bytesRead != UINT32SIZE)
            {
                printm("file is corrupt or not the right kind...");
                bError = true;
                return;
            }

            if (numProgs == 0)
            {
                printm("no programs in file!");
                bError = true;
                return;
            }

            printm("sending " + String(numProgs) + " programs to S950/S900...");

            // allocate memory for largest program with up to 64 keygroups
            // a single program has PRG_FILE_HEADER_SIZE + (X*PROGKEYGROUPSIZ) + 2 for
            // checksum and EEX bytes
            UInt32 bufSize = PRG_FILE_HEADER_SIZE + (MAX_KEYGROUPS*PROGKEYGROUPSIZ) + 2;
            buf = new Byte[bufSize];

            if (buf == NULL)
            {
                printm("can't allocate buffer for program and keygroups!");
                bError = true;
                return;
            }

            String dots; // progress display

                         // request each program and add it to file on the fly
            for (UInt32 ii = 0; ii < numProgs; ii++)
            {
                Application->ProcessMessages();

                // read program-size from file.
                // a program in the file is already formatted for the S950 and
                // includes the BEX, checksum and EEX.
                UInt32 progSize;
                bytesRead = FileRead(iFileHandle, &progSize, UINT32SIZE);

                if (bytesRead != UINT32SIZE)
                {
                    printm("file is corrupt or not the right kind...");
                    bError = true;
                    return;
                }

                if (progSize > bufSize)
                {
                    printm("error, progSize > bufSize at index: " + String(ii));
                    bError = true;
                    return;
                }

                // copy program in file to buf
                bytesRead = FileRead(iFileHandle, buf, progSize);

                if (bytesRead != progSize)
                {
                    printm("file is corrupt... (index=" + String(ii) + ")");
                    bError = true;
                    return;
                }

                if (buf[0] != BEX)
                {
                    printm("buffer does not contain BEX! (index=" + String(ii) + ")");
                    bError = true;
                    return;
                }

                if (buf[progSize - 1] != EEX)
                {
                    printm("buffer does not contain EEX! (index=" + String(ii) + ")");
                    bError = true;
                    return;
                }

                // Must delay or programs won't be transmitted!
                DelayGpTimer(DELAY_BETWEEN_EACH_PROGRAM_TX);

                // send program to Akai S950/S900
                if (!comws(progSize, buf, false))
                    return;

                dots += '.';
                printm(dots);
            }
        }
        catch(...)
        {
            printm("exception thrown while sending programs!");
            bError = true;
        }
    }
    __finally
    {
        if (iFileHandle)
            FileClose(iFileHandle);

        if (buf != NULL)
            delete[] buf;

        if (!bError)
            printm(String(numProgs) + " programs successfully sent!");
        else
            printm("unable to write programs...");
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::WMDropFile(TWMDropFiles &Msg)
{
    try
    {
        //get dropped files count
        int cnt = ::DragQueryFileW((HDROP)Msg.Drop, -1, NULL, 0);

        if (cnt != 1)
            return; // only one file!

        wchar_t wBuf[MAX_PATH];

        // Get first file-name
        if (::DragQueryFileW((HDROP)Msg.Drop, 0, wBuf, MAX_PATH) > 0)
        {
            // Load and convert file as per the file-type (either plain or rich text)
            WideString wFile(wBuf);

            // don't process this drag-drop until previous one sets m_DragDropFilePath = ""
            if (m_DragDropFilePath.IsEmpty() && !wFile.IsEmpty())
            {
                String sFile = String(wFile);
                if (FileExists(sFile))
                {
                    m_DragDropFilePath = sFile;
                    Timer1->Enabled = false; // stop timer (just in-case!)
                    Timer1->Interval = 50;
                    Timer1->OnTimer = Timer1FileDropTimeout; // set handler
                    Timer1->Enabled = true; // fire event to send file
                }
            }
        }
    }
    catch (...) {}
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::Timer1FileDropTimeout(TObject *Sender)
{
    Timer1->Enabled = false;
    if (!m_DragDropFilePath.IsEmpty())
    {
        String Ext = ExtractFileExt(m_DragDropFilePath).LowerCase();
        if (Ext == ".prg")
            PutPrograms(m_DragDropFilePath);
        else
            PutSample(m_DragDropFilePath);
        m_DragDropFilePath = "";
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::DelayGpTimer(int time)
{
    m_sysBusy = true;
    StartGpTimer(time);
    while (!IsGpTimeout())
        Application->ProcessMessages();
    StopGpTimer();
    m_sysBusy = false;
}
//---------------------------------------------------------------------------
bool __fastcall TFormMain::IsGpTimeout(void)
{
    return m_gpTimeout;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::StopGpTimer(void)
{
    Timer1->Enabled = false;
    Timer1->OnTimer = NULL;
    m_gpTimeout = false;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::StartGpTimer(int time)
{
    Timer1->Enabled = false;
    Timer1->OnTimer = Timer1GpTimeout;
    Timer1->Interval = time;
    Timer1->Enabled = true;
    m_gpTimeout = false;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::Timer1GpTimeout(TObject *Sender)
{
    // used for midi-diagnostic function
    Timer1->Enabled = false;
    m_gpTimeout = true;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::Timer1RxTimeout(TObject *Sender)
{
    Timer1->Enabled = false;
    m_rxTimeout = true;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::Timer1TxTimeout(TObject *Sender)
{
    Timer1->Enabled = false;
    m_txTimeout = true;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::ComboBoxRs232Change(TObject *Sender)
{
    SetComPort(ComboBoxRs232->Text.ToIntDef(DEF_RS232_BAUD_RATE));
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::ComboBoxRs232Select(TObject *Sender)
{
    SetComPort(ComboBoxRs232->Text.ToIntDef(DEF_RS232_BAUD_RATE));
    Memo1->SetFocus();
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::SetComPort(int baud)
{
    // From Akai protocol: "Hardware handshake using CTS/RTS. Handshake need not
    // be used on rates below 50000 baud, in which case RTS should be tied high.�
    ApdComPort1->DonePort();

    // hwfUseDTR, hwfUseRTS, hwfRequireDSR, hwfRequireCTS
    THWFlowOptionSet hwflow;
    bool rts;
    if (baud >= 50000 || m_force_hwflow)
    {
        hwflow = (THWFlowOptionSet() << hwfUseRTS << hwfRequireCTS);
        rts = false; // state of the RTS line low
    }
    else
    {
        hwflow.Clear();
        rts = true; // state of the RTS line high
    }

    ApdComPort1->Baud = baud;
    ApdComPort1->HWFlowOptions = hwflow;
    ApdComPort1->RTS = rts;
    ApdComPort1->ComNumber = 0; // COM1
    ApdComPort1->Baud = baud;
    ApdComPort1->DataBits = 8;
    ApdComPort1->StopBits = 1;
    ApdComPort1->Parity = pNone;
    ApdComPort1->AutoOpen = true;

    m_baud = baud;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::trimright(Byte* pStr)
{
    int len = 0;
    for (; len < MAX_NAME_S900; len++)
        if (pStr[len] == 0)
            break;

    while (len > 0)
    {
        if (pStr[len - 1] != ' ')
        {
            pStr[len] = 0;
            break;
        }

        len--;
    }

    pStr[len] = 0;
}
//---------------------------------------------------------------------------
// returns TRUE if strings match. case-insensitive, no trimming
// (Strings in the S900/S950 are in ASCII)
bool __fastcall TFormMain::StrCmpCaseInsens(char* sA, char* sB, int len)
{
    return AnsiString(sA, len).LowerCase() == AnsiString(sB, len).LowerCase();
}
//---------------------------------------------------------------------------
// display data obtained via receive()
void __fastcall TFormMain::display_hex(Byte buf[], int count)
{
    String sTemp;
    for (int ii = 0; ii < count; ii++)
    {
        if ((ii % 16) == 0)
        {
            if (!sTemp.IsEmpty())
            {
                printm(sTemp);
                sTemp = "";
            }
        }
        sTemp += IntToHex((int)buf[ii], 2) + " ";
    }
    // print what remains...
    if (!sTemp.IsEmpty())
        printm(sTemp);
}
//---------------------------------------------------------------------------
// chan defaults to 0 (midi channel 1)
bool __fastcall TFormMain::exmit(int samp, int mode, bool bDelay)
{
    Byte midistr[8];

    midistr[0] = BEX;
    midistr[1] = AKAI_ID;
    midistr[2] = 0; // midi channel
    midistr[3] = (Byte)mode;
    midistr[4] = S900_ID;
    midistr[5] = (Byte)samp;
    midistr[6] = 0;
    midistr[7] = EEX;

    return comws(8, midistr, bDelay);
}
//---------------------------------------------------------------------------
// SDS: F0 7E cc 03 ss ss F7
bool __fastcall TFormMain::cxmit(int samp, int mode, bool bDelay)
{
    Byte midistr[7];

    midistr[0] = BEX;
    midistr[1] = SYSTEM_COMMON_NONREALTIME_ID;
    midistr[2] = (Byte)mode;
    midistr[3] = (Byte)samp; // lsb samp
    midistr[4] = (Byte)(samp >> 8); // msb samp
    midistr[5] = EEX;

    return comws(6, midistr, bDelay);
}
//---------------------------------------------------------------------------
// SDS: F0 7E cc 7F pp F7
// blockct is only used for sample-dump standard to ACK the block just received
bool __fastcall TFormMain::chandshake(int mode, int blockct)
{
    Byte midistr[ACKSIZ];

    midistr[0] = BEX;
    midistr[1] = SYSTEM_COMMON_NONREALTIME_ID;
    midistr[2] = (Byte)mode;
    midistr[ACKSIZ-1] = EEX;

    return comws(ACKSIZ, midistr, false); // no delay
}
//---------------------------------------------------------------------------
// opens midi In/Out automatically or flushes RS232 In/Out buffers
bool __fastcall TFormMain::comws(int count, Byte* ptr, bool bDelay)
{
    // this delay is necessary to give the older-technology S900/S950 time
    // to digest any previous commands such as sending a program, a sample
    // data-block, request sysex on, catalog, etc. Without it, all programs
    // may not get assimilated by the target-machine (for certain!)
    if (bDelay)
        DelayGpTimer(25); // add 25ms between transmits

    try
    {
        Timer1->Enabled = false; // stop timeout timer (should already be stopped)
        Timer1->OnTimer = Timer1TxTimeout; // set handler
        m_txTimeout = false;

        ApdComPort1->FlushInBuffer();
        ApdComPort1->FlushOutBuffer();

        Timer1->Interval = TXRS232TIMEOUT; // 3.1 seconds
        Timer1->Enabled = true; // start timeout timer

        for (int ii = 0; ii < count; ii++)
        {
            while (ApdComPort1->OutBuffFree < 1)
            {
                Application->ProcessMessages();

                if (m_txTimeout)
                {
                    printm("rs232 transmit timeout!");
                    return false;
                }
            }

            ApdComPort1->PutChar(*ptr++);

            // reset timeout
            Timer1->Enabled = false; // stop timer
            m_txTimeout = false;
            Timer1->Interval = TXRS232TIMEOUT; // 3.1 seconds
            Timer1->Enabled = true; // start timeout timer
        }
    }
    __finally
    {
        Timer1->Enabled = false;
        Timer1->OnTimer = NULL; // clear handler
        m_txTimeout = false;
    }

    return true;
}
//---------------------------------------------------------------------------
// returns 0 if acknowledge received ok
int __fastcall TFormMain::get_ack(int blockct)
{
    if (receive(0))
    {
        printm("timeout receiving acknowledge (ACK)! (block=" + String(blockct) + ")");
        return 1;
    }

    if (m_rxByteCount == (unsigned int)m_ack_size)
    {
        int idx = 2; // point to the ACKS/NAKS/ASD
        Byte c = TempArray[idx];

        if (c == ACKS)
            return 0; // ok!

        if (c == NAKS)
        {
            printm("packet \"not-acknowledge\" (NAK) received! memory full? (block=" + String(blockct) + ")");
            return 2;
        }

        if (c == ASD)
        {
            printm("packet \"abort sample dump\" (ASD) received! memory full? (block=" + String(blockct) + ")");
            return 3;
        }

        if (c == PSD)
        {
            printm("packet \"pause sample dump\" (PSD) received! memory full? (block=" + String(blockct) + ")");
            return 3;
        }

        printm("bad acknowledge, unknown code! (block=" + String(blockct) + ", code=" + String((UInt32)c) + ")");
        return 4;
    }

    printm("bad acknowledge, wrong size! (block=" + String(blockct) + ", size=" + String(m_rxByteCount) + ")");
    return 5;
}
//---------------------------------------------------------------------------
void __fastcall TFormMain::FormKeyDown(TObject *Sender, WORD &Key, TShiftState Shift)
{
    if (Key == VK_ESCAPE)
        m_abort = true;
}
//---------------------------------------------------------------------------

