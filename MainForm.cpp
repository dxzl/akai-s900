//---------------------------------------------------------------------------
//Software written by Scott Swift 2016 - This program is distributed under the
//terms of the GNU General Public License.
//---------------------------------------------------------------------------
#pragma package(smart_init)
#include <vcl.h>
#pragma hdrstop

#include "MainForm.h"
#include "RegHelper.h"
#include "stdio.h"
//---------------------------------------------------------------------------
#pragma link "AdPort"
#pragma link "OoMisc"
#pragma resource "*.dfm"
TFormS900 *FormS900;
//---------------------------------------------------------------------------
__fastcall TFormS900::TFormS900(TComponent* Owner)
        : TForm(Owner)
{
#if (AKI_FILE_HEADER_SIZE != 72)
    printm("\r\nWARNING: sizeof(PSTOR) != 72\r\n"
             "TO DEVELOPER: sizeof(PSTOR) MUST be " + String(72) + " bytes\r\n"
             "to maintain compatibility with old .AKI files!");
#endif
#if (sizeof(S900CAT) != 12)
    printm("\r\nWARNING: sizeof(S900CAT) != 12\r\n"
             "TO DEVELOPER: sizeof(S900CAT) MUST be " + String(12) + " bytes!");
#endif
	// use the following # for PSTOR_STRUCT_SIZ in MainForm.h!
    //printm("sizeof(PSTOR):" + String(sizeof(PSTOR)));
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::FormCreate(TObject *Sender)
{
	Timer1->OnTimer = NULL;
	Timer1->Enabled = false;

    m_byteCount = 0;
    m_numSampEntries = 0;
    m_numProgEntries = 0;
    m_DragDropFilePath = "";
    m_rxTimeout = false;
    m_gpTimeout = false;
    m_systemBusy = false;

    // read settings from registry HKEY_CURRENT_USER
    // \\Software\\Discrete-Time Systems\\AkaiS900
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
                    pReg->WriteSetting(S9_REGKEY_VERSION, VERSION_STR);

                    // cmd reg delete "HKCU\Software\Discrete-Time Systems\AkaiS900" /f
                    printm("This app stores its settings in the windows registry.\r\n"
                      "To delete settings, go to Start => Run and type \"cmd\"\r\n"
                      "In the window type the line below and press enter:\r\n\r\n"
                      "reg delete \"HKCU\\Software\\Discrete-Time Systems\\AkaiS900\" /f\r\n"
                      "(or: Start => Run, \"regedit\" and search for \"AkaiS900\")\r\n");
                }

				pReg->ReadSetting(S9_REGKEY_BAUD, m_baud, 38400);
                pReg->ReadSetting(S9_REGKEY_USE_RIGHT_CHAN, m_use_right_chan, true);
                pReg->ReadSetting(S9_REGKEY_AUTO_RENAME, m_auto_rename, true);
                pReg->ReadSetting(S9_REGKEY_FORCE_HWFLOW, m_force_hwflow, false);

                // we tried this and the S950 rejects 16-bit bits-per-word in the
                // sample-header... sadly
//                pReg->ReadSetting(S9_REGKEY_TARGET_S950, m_target_S950, false);
                m_target_S950 = false;
//                pReg->ReadSetting(S9_REGKEY_USE_SMOOTH_QUANTIZATION, m_use_smooth_quantization, false);
                m_use_smooth_quantization = false;
            }
            else
            {
                ShowMessage("Unable to read settings from the registry!");
                m_baud = 38400;
                m_use_right_chan = true;
                m_auto_rename = true;
                m_force_hwflow = false;
                m_target_S950 = false;
                m_use_smooth_quantization = false;
            }
        }
        catch(...)
        {
            ShowMessage("Unable to read settings from the registry!");
            m_baud = 38400;
            m_use_right_chan = true;
            m_auto_rename = true;
            m_force_hwflow = false;
            m_target_S950 = false;
            m_use_smooth_quantization = false;
        }

        printm(VERSION_STR);
        printm("Click \"Menu\" and select \"Help\"...");
    }
    __finally
    {
        try { if (pReg != NULL) delete pReg; } catch(...) {}
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
//    MenuTargetS950->Checked = m_target_S950;
//    MenuUseSmoothQuantization->Checked = m_use_smooth_quantization;

    ComboBox1->Text = String(m_baud);
    SetComPort(m_baud);

    //enable drag&drop files
    ::DragAcceptFiles(this->Handle, true);
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::FormClose(TObject *Sender, TCloseAction &Action)
{
    // save settings to registry HKEY_CURRENT_USER
    // \\Software\\Discrete-Time Systems\\AkaiS900
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
//            pReg->WriteSetting(S9_REGKEY_TARGET_S950, m_target_S950);
//            pReg->WriteSetting(S9_REGKEY_USE_SMOOTH_QUANTIZATION, m_use_smooth_quantization);
        }
    }
    __finally
    {
        try { if (pReg != NULL) delete pReg; } catch(...) {}
    }
}
//---------------------------------------------------------------------------
// Get sample routines
//---------------------------------------------------------------------------
// all you need to call to receive a sample into a file...
// returns 0 if success.
int __fastcall TFormS900::GetSample(int samp, String fileName)
{
    int iFileHandle = 0;

    try
	{
        if (!FileExists(fileName))
            iFileHandle = FileCreate(fileName);
        else
            iFileHandle = FileOpen(fileName, fmShareDenyNone | fmOpenReadWrite);

        if (iFileHandle == 0)
        {
            printm("error opening file, bad handle!");
            return 4;
        }

        unsigned __int32 my_magic = MAGIC_NUM_AKI;
        PSTOR ps = {0};

        exmit(0, SECRE, false); // request common reception enable

        // first interrogate S900 to get sample parameters...

        // populate global samp_parms array
        if (get_comm_samp_parms(samp))
            return 1; // could not get sample parms

        // populate global samp_hedr array
        if (get_comm_samp_hedr(samp))
            return 2; // could not get header data

        // fill global PSTOR struct...
        decode_sample_info(&ps);

        print_ps_info(&ps);

        // write the magic number and header...
        FileWrite(iFileHandle, (char*)&my_magic, UINT32SIZE);
        FileWrite(iFileHandle, (char*)&ps, AKI_FILE_HEADER_SIZE); // write the full 72 byte struct

        if (get_samp_data(&ps, iFileHandle))
            return 3; // could not get sample data
    }
    __finally
    {
        exmit(0,SECRD, true); // request common reception disable

        if (iFileHandle)
            FileClose(iFileHandle);
    }

    return 0;
    // 1=no parms, 2=no header, 3=no samp data, 4=bad file handle
}
//---------------------------------------------------------------------------
// puts samp_parms[] and samp_hedr[] arrays info into PSTOR struct (ps)
void __fastcall TFormS900::decode_sample_info(PSTOR* ps)
{
    // FROM AKAI EXCLUSIVE SAMPLE PARAMETERS... (do this before decoding header)

    // clear spare-byte fields
    for(int ii = 0; ii < PSTOR_SPARE_COUNT; ii++)
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
    ps->bits_per_word = (unsigned __int16)samp_hedr[5]; // 4

    // sampling period in nS 15259-500000
    ps->period = decode_hedrTB(&samp_hedr[6]); // 3

    // NOW USING VALUES IN samp_parms FOR ITEMS BELOW!!!!!!!!!!!!!!!!!!

    // number of sample words 200-475020
    // ps->totalct = decode_hedrTB((Byte*)&samp_hedr[9]); // 3

    // loop start point (non-looping mode if >= endidx-5)
    // int loopstart = decode_hedrTB((Byte*)&samp_hedr[12]); // 3

    // loop end point (S900/S950 takes this as end point of the sample)
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
// returns 0=OK, 1=writerror, 2=com error, 4=bad chksm, 8=wrong # words, 16=bad # bits per sample
// receive sample data blocks and write them to a file, print a progress display
// all functions here print their own error messages ao you can just
// look for a return value of 0 for success.
int __fastcall TFormS900::get_samp_data(PSTOR* ps, int handle)
{
    // always 120 byte packets, S900 is max 60 2-byte words in 14-bits
    // and S950 is max 40 3-byte words in 16-bits
    //
    // we recieve any # bits per word and properly format it as 40 or 60
    // 16-bit 2's compliment sample-words in rbuf
    __int16 *rbuf = NULL;
    int status = 0;

    try
    {
        unsigned int count = 0;
        int writct, retstat;
        String dots = "";
        int blockct = 0;

        int bits_per_word = ps->bits_per_word;

        int bytes_per_word = bits_per_word/7;
        if (bits_per_word % 7)
            bytes_per_word++;

        if (DATA_PACKET_SIZE % bytes_per_word)
        {
            printm("can't fit expected samples into 120 byte packets: " +
                                                String(bits_per_word));
            status = 16; // bad bits-per-sample
            return status;
        }

		// should be 40 (16-bit samples) or 60 (8-14 bit samples)
        int words_per_block = DATA_PACKET_SIZE/bytes_per_word;

        unsigned int total_words = ps->totalct;

        rbuf = new __int16[words_per_block];

        for(;;)
        {
            chandshake(ACKS);

            if (count >= ps->totalct)
                break;

            if ((retstat = get_comm_samp_data(rbuf, bytes_per_word,
                    words_per_block, bits_per_word, blockct+1)) != 0)
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
            writct = words_per_block * UINT16SIZE;

            // write only up to totalct words...
            if (count > total_words)
                writct -= (int)(count-total_words) * UINT16SIZE;

			// write to file
            if (FileWrite(handle, rbuf, writct) < 0)
            {
                chandshake(ASD); // abort dump
                printm("error writing sample data to file! (block=" + String(blockct+1) + ")");
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
              printm("expected " + String(total_words) + " bytes, but received " + String(count) + "!");

            status |= 8; // wrong number of words received
        }
    }
    __finally
    {
        if (rbuf)
            delete [] rbuf;
    }

    return status;
}
//---------------------------------------------------------------------------
// returns: 0=OK, 1=receive error, 2=bad checksum
// receive 8-16-bit sample data into 16-bit words
// converts a block of raw sample data to gets a sample's raw data into bufptr and validates the checksum
//
// # words per 120 byte block is generally 60 8-14 bit samples
// or 40 15-16 bit samples...
int __fastcall TFormS900::get_comm_samp_data(__int16* bufptr,
  int bytes_per_word, int words_per_block, int bits_per_word, int blockct)
{
    try
    {
        int errorCode = receive(DATA_PACKET_SIZE+2);

        // receive data sample data block from serial port
        // and store in tempBuf
        if (errorCode < 0)
        {
            FormS900->printm("did not receive expected " + String(DATA_PACKET_SIZE+2) +
					" byte data-block! (block=" + String(blockct) + ")");
            FormS900->printm("(receiver error code is: " + String(errorCode) + ")");
            return 1;
        }

        Byte* cp = TempArray+1;
        Byte checksum = 0;

        __int16 baseline = (__int16)(1 << (bits_per_word-1));

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
                int shift_count = bits_per_word - (jj*7);

                unsigned __int16 val = (unsigned __int16)*cp;
                checksum ^= *cp++;

                unsigned __int16 or_val;
                if (shift_count >= 0)
                    or_val = (unsigned __int16)(val << shift_count);
                else
                    or_val = (unsigned __int16)(val >> -shift_count);

                tempint |= or_val;
            }

            tempint -= baseline; // convert to two's compliment
            *bufptr++ = tempint;
        }

        if (checksum != *cp)
        {
            printm("bad checksum for data-block! (block=" + String(blockct) + ")");
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
// get a sample's header info into the global TempArray
int __fastcall TFormS900::get_comm_samp_hedr(int samp)
{
    // request sample dump
    cxmit(samp, RSD, true);

    // see if chars waiting from serial port...
    if (receive(HEDRSIZ))
    {
        printm("timeout receiving sample header!");
        return 1;
    }

    // copy into buf
    memcpy(samp_hedr, TempArray, HEDRSIZ);

    return 0; // 0=OK, 1=com error
}
//---------------------------------------------------------------------------
// returns: 0=OK, 1=receive error, 2=wrong bytes, 3=bad checksum
// gets a sample's sysex extended parameters and puts them in the
// global TempArray... also validates the checksum
int __fastcall TFormS900::get_comm_samp_parms(int samp)
{
    // request sample parms
    exmit(samp, RSPRM, true);

	// receive complete SYSEX message from serial port..., put into TempArray
    if (receive(0))
    {
        printm("timeout receiving sample parameters!");
        return 1;
    }

    if (m_byteCount != PARMSIZ)
    {
        printm("received wrong bytecount for sample parameters: " + String(m_byteCount));
        return 2;
    }

    Byte* cp = TempArray+7; // point past header to sample-name
    Byte checksum = 0;

    // checksum of buffer minus 7-byte header and checksum and EEX
    for (int ii = 0 ; ii < PARMSIZ-9 ; ii++)
        checksum ^= *cp++;

    if (checksum != *cp)
    {
        printm("bad checksum for sample parameters!");
        return 3; // bad checksum
    }

    // copy TempArray into samp_parms
    memcpy(samp_parms, TempArray, PARMSIZ);

    return 0;
}
//---------------------------------------------------------------------------
Byte __fastcall TFormS900::decode_parmsDB(Byte* source)
{
    Byte c = *source++;
    c |= (Byte)(*source << 7);
  	return c;
}
//---------------------------------------------------------------------------
// make sure sizeof dest buffer >= numchars+1!
void __fastcall TFormS900::decode_parmsDB(Byte* dest, Byte* source, int numchars)
{
    for (int ii = 0 ; ii < numchars ; ii++ )
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
unsigned __int32 __fastcall TFormS900::decode_parmsDD(Byte* tp)
{
    unsigned __int32 acc;

    tp += 6;

    acc = 0;
    acc |= *tp | (*(tp+1) << 7);

    for (int ii = 0; ii < 3; ii++)
    {
        acc <<= 8;
        tp -= 2;
        acc |= *tp | (*(tp+1) << 7);
    }

    return acc;
}
//---------------------------------------------------------------------------
// decode a 16-bit value in 4-bytes into an __int32
unsigned __int32 __fastcall TFormS900::decode_parmsDW(Byte* tp)
{
    unsigned __int32 acc;

    tp += 2;

    acc = 0;
    acc |= *tp | (*(tp+1) << 7);

    acc <<= 8;
    tp -= 2;
    acc |= *tp | (*(tp+1) << 7);

    return acc;
}
//---------------------------------------------------------------------------
// decode a 21-bit value in 3-bytes into an __int32
unsigned __int32 __fastcall TFormS900::decode_hedrTB(Byte* tp)
{
  	unsigned __int32 acc;

    tp += 2;

    acc = (unsigned int)*tp--;
	acc = (acc << 7) | (unsigned int)*tp--;
    acc = (acc << 7) | (unsigned int)*tp;

    return acc;
}
//---------------------------------------------------------------------------
// Send sample routines
//---------------------------------------------------------------------------
void __fastcall TFormS900::PutSample(String sFilePath)
{
    if (m_systemBusy) return;

    Byte *fileBuf = NULL;
    int iFileHandle = 0;

    try
    {
        m_systemBusy = true;

        ListBox1->Clear();
        Memo1->Clear();
        ApdComPort1->FlushInBuffer();
        ApdComPort1->FlushOutBuffer();

        int iFileLength;
        int iBytesRead;
        int sampIndex;
        PSTOR ps = {0};
        int blockct;
        char newname[MAX_NAME_S900+1];

        try
        {
            if (sFilePath.IsEmpty() || !FileExists(sFilePath))
            {
              printm("file does not exist!");
              return;
            }

            // Load file
            printm("file path: \"" + sFilePath + "\"");

            // allow file to be opened for reading by another program at the same time
            iFileHandle = FileOpen(sFilePath, fmShareDenyNone | fmOpenRead);

            if (iFileHandle == 0)
            {
              printm("unable to open file, handle is 0!");
              return;
			}

            // get file length
            iFileLength = FileSeek(iFileHandle,0,2);
            FileSeek(iFileHandle,0,0); // back to start

            if (iFileLength == 0)
            {
              FileClose(iFileHandle);
              printm("unable to open file, length is 0!");
              return;
            }

            fileBuf = new Byte[iFileLength+1];

            if (fileBuf == NULL)
            {
              FileClose(iFileHandle);
              printm("unable to allocate " + String(iFileLength+1) + " bytes of memory!");
              return;
            }

        //printm("allocated " + String(iFileLength+1) + " bytes...");

            try
            {
              iBytesRead = FileRead(iFileHandle, fileBuf, iFileLength);
              //printm("read " + String(iBytesRead) + " bytes...");
            }
            catch(...)
            {
              printm("unable to read file into buffer...");
              return;
            }

            // finished with the file...
            FileClose(iFileHandle);
            iFileHandle = 0;

            // copy up to the first 10 chars of file-name without extension
            // into newname and pad the rest with spaces, terminate
            String sName = ExtractFileName(sFilePath);
            int lenName = sName.Length();
            bool bStop = false;
            for (int ii = 1; ii <= MAX_NAME_S900; ii++)
            {
              if (!bStop && (ii > lenName || sName[ii] == '.'))
                  bStop = true;

			  if (bStop)
                newname[ii-1] = ' ';
              else
                newname[ii-1] = sName[ii];
            }
            // terminate it
            newname[MAX_NAME_S900] = '\0';

            printm("serial baud-rate: " + String(ApdComPort1->Baud));

            int machine_max_bits_per_word = m_target_S950 ?
                        S950_BITS_PER_WORD : S900_BITS_PER_WORD;

            int bytes_per_word = machine_max_bits_per_word/7;

            if (machine_max_bits_per_word % 7)
                bytes_per_word++;

            // bytes_per_word needs to be be 120/2 = 60 for the S900...
            // or for the S950 it should be 120/2 = 60 or for
            // 16-bit samples, 120/3 = 40. there should be no remainder
            if (DATA_PACKET_SIZE % bytes_per_word)
            {
                printm("bytes_per_word of " + String(bytes_per_word) +
                     " does not fit evenly into 120 byte packet!");
                return;
            }

            int words_per_block = DATA_PACKET_SIZE/bytes_per_word;

            bool bSendAborted = false; // flag set if we receive a not-acknowledge on any data-packet

            if (ExtractFileExt(sName).LowerCase() != ".aki") // Not an AKI file? (try WAV...)
            {
                if (iBytesRead < 45)
                {
                  printm("bad file (1)");
                  return;
                }

                if (!StrCmpCaseInsens((char*)&fileBuf[0], "RIFF", 4))
                {
                  printm("bad file (2) [no \'RIFF\' preamble!]");
                  return;
                }

                int file_size = *(__int32*)&fileBuf[4];
                if (file_size+8 != iBytesRead)
                {
				  printm("bad file (3), (file_size = " +
                   String(file_size+8) + ", iBytesRead = " +
                      String(iBytesRead) + ")!");
                  return;
                }

                if (!StrCmpCaseInsens((char*)&fileBuf[8], "WAVE", 4))
                {
                  printm("bad file (4) [no \'WAVE\' preamble!]");
                  return;
                }

                // Search file for "fmt " block
                Byte* headerPtr = fileBuf;
                // NOTE: the FindSubsection will return headerPtr by reference!
                __int32 headerSize = FindSubsection(headerPtr, "fmt ", iBytesRead);
                if (headerSize < 0)
                {
                  printm("bad file (4) [no \'fmt \' sub-section!]");
                  return;
                }

                // Length of the format data in bytes is a four-byte int at
                // offset 16. It should be at least 16 bytes of sample-info...
                if (headerSize < 16)
                {
                  printm("bad file (6) [\'fmt \' sub-section is < 16 bytes!]");
                  return;
                }

                // Search file for "data" block
                // (Left-channel data typically begins after the header
                // at 12 + 4 + 4 + 16 + 4 + 4 - BUT - could be anywhere...)
                Byte* dp = fileBuf;
                // NOTE: the FindSubsection will return dp by reference!
                __int32 dataLength = FindSubsection(dp, "data", iBytesRead);
                if (dataLength < 0)
                {
                  printm("bad file (4) [no \'data\' sub-section!]");
                  return;
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
                  return;
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
                  printm("bits per sample out of range: " +
                                       String(BitsPerWord));
                  return;
                }

                int BytesPerWord = BitsPerWord/8;
                if (BitsPerWord % 8) // remaining bits?
                  BytesPerWord++; // round up

                if (BytesPerWord > 8)
                {
                  printm("error: can't handle samples over 64-bits!");
                  return;
                }

                if (NumChannels * BytesPerWord != BytesPerFrame)
                {
                  printm("error: (NumChannels * BytesPerWord != BytesPerFrame)");
                  return;
                }

                // there should be no "remainder" bytes...
                if (dataLength % (NumChannels * BytesPerWord))
                {
                  printm("error: incomplete data-block!");
				  return;
                }

                printm("bytes per sample: " + String(BytesPerWord));

                int TotalFrames = dataLength / (NumChannels * BytesPerWord);
                printm("number of frames: " + String(TotalFrames));

                // this is printed in the output parameters...
                //printm("sample name: " + String(newname).TrimRight());

                // make sure we have a file-length that accomodates the expected data-length!
                if (dp+dataLength > fileBuf+iBytesRead)
                {
                  printm("error: [dp+dataLength > fileBuf+iBytesRead!]");
                  return;
                }

                // Need to populate a PSTOR structure for S900
                ps.loopstart = 0; // first replay point for looping (4 bytes)
                ps.endpoint = TotalFrames-1; // end of play index (4 bytes)
                ps.looplen = ps.endpoint; // loop length (4 bytes)

                unsigned __int32 tempfreq = SampleRate;
                if (tempfreq > 49999)
                  tempfreq = 49999;
                ps.freq = (unsigned __int32)tempfreq; // sample freq. in Hz (4 bytes)

                ps.pitch = 960; // pitch - units = 1/16 semitone (4 bytes) (middle C)
                ps.totalct = TotalFrames; // total number of words in sample (4 bytes)
                ps.period = 1.0e9/(double)ps.freq; // sample period in nanoseconds (8 bytes)

                // 8-14 bits S900 or 8-16-bits S950
                // (this will be the bits-per-word of the sample residing on the machine)
                // (DO THIS BEFORE SETTING shift_count!)
                ps.bits_per_word =
                  (unsigned __int16)((BitsPerWord > machine_max_bits_per_word) ?
                                machine_max_bits_per_word : BitsPerWord);

                // positive result is the amount of right shift needed (if any)
                // to down-convert the wav's # bits to the desired # bits
                // (example: Wave-file's BitsPerWord is 16 and ps.bits_per_word
                // max is 14 bits for S900, result is "need to right-shift 2")
                // (DO THIS AFTER LIMITING ps.bits_per_word TO machine_max_bits_per_word!)
                int shift_count = BitsPerWord-ps.bits_per_word;

                strncpy(ps.name, newname, MAX_NAME_S900); // ASCII sample name (10 bytes)
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
                if ((sampIndex = FindIndex(ps.name)) < 0)
                    return;

                // encode samp_hedr and samp_parms arrays
                encode_sample_info(sampIndex, &ps);

                print_ps_info(&ps);

                // request common reception enable
                exmit(0, SECRE, true);

                // transmit sample header info
                comws(HEDRSIZ, samp_hedr, true);

                // wait for acknowledge
                if (get_ack(0))
                    return;

                int FrameCounter = 0; // We already processed the header

                blockct = 0;

                // if Stereo and User checked "send right channel"
                // point to first right-channel sample-datum
                if (NumChannels > 1 && m_use_right_chan)
                    dp += BytesPerWord;

                // 1/2 the full numeric range for the # bits per sample
                unsigned __int64 wav_baseline = (1 << (BitsPerWord-1));

                // catch an overflow if we are rounding acc up (see below)
                unsigned __int64 max_val = (1 << ps.bits_per_word)-1;

                // init progress display
                int divisor = (ps.totalct <= 19200) ? 8 : 32;
                String dots;

                // for each frame in WAV-file (one-sample of 1-8 bytes...)
                for(;;)
                {
                    // End of file?
                    if (FrameCounter >= TotalFrames)
                        break;

                    // read and encode block of 60 frames...

                    // Strategy: encode WAV samples of 8-64 bits
                    // as unsigned 14 (or 16-bit) (two-byte S900 "SW" format)
                    // S900 is 12-bits but can receive 14-bits.
                    // 8-bit wav-format is already un-signed, but
                    // over 8-bits we need to convert from signed to
                    // unsigned.  All values need to be converted into
                    // a 14-bit form for S900...

                    // For an S950, the data are encoded into 3 bytes per
                    // sample - 40 samples per 120 byte block.
                    // For an S900, the data are encoded into 2 bytes per
                    // sample - 60 samples per 120 byte block.

                    Byte checksum = 0; // clear checksum

                    // send the block #
                    Byte temp = (Byte)(blockct & 0x7f);
                    comws(1, &temp, false); // no delay

                    register __int64 acc; // sample accumulator

                    for (int ii = 0; ii < words_per_block; ii++)
                    {
                        if (FrameCounter >= TotalFrames)
                            acc = 0L;
                        else
                        {
                            // one-byte wave samples are un-signed by default
                            if (BytesPerWord < 2) // one byte?
                                acc = *dp;
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
                                if (dp[BytesPerWord-1] & 0x80)
                                    acc = -1; // set all 1s
                                else
                                    acc = 0;

                                // load accumulator with 8-64 bit sample
                                // (Microsoft WAV sample-data are in Intel little-endian
                                // byte-order. left-channel sample appears first for a
                                // stereo WAV file, then the right-channel.)
                                for (int ii = BytesPerWord-1; ii >= 0; ii--)
                                {
                                    acc <<= 8; // zero-out new space and shift previous
                                    acc |= dp[ii];
                                }

                                // right-justify so we can add baseline
                                acc >>= (8*BytesPerWord)-BitsPerWord;

                                // convert from 2's compliment to offset-binary:
                                // store N + (1<<(BitsPerWord-1)) in a 64-bit
                                // int.
                                acc += wav_baseline;

                                // convert down to 14 or 16-bits if over...
                                if (shift_count > 0)
                                {
                                    // shift msb of discarded bits to lsb of val
                                    acc >>= shift_count-1;

                                    bool bRoundUp = acc & 1; // need to round up?

                                    // discard msb of discarded bits...
                                    acc >>= 1;

                                    if (bRoundUp && acc != max_val)
                                        acc++;
                                }
                            }
                        }

						// Send sample
                        xmit((unsigned __int16)acc, bytes_per_word,
                                                 ps.bits_per_word, checksum);

                        dp += BytesPerFrame; // Next frame
                        FrameCounter++;
                    }

                    comws(1, &checksum, false); // send checksum (no delay)

                    // do the ..... progress indicator
                    if (blockct % divisor == 0)
                    {
                        dots += ".";
                        FormS900->printm(dots);
                    }

                    // wait for acknowledge
                    if (get_ack(blockct+1) != 0)
                    {
                        // sample send failed!

                        unsigned int countSent = blockct*words_per_block;
                        printm("sent " + String(countSent) + " of " +
                                          String(ps.totalct) + " sample words!");

                        // limits
                        if (countSent < ps.totalct)
                        {
                            ps.totalct = countSent;
                            ps.endpoint = ps.totalct-1;
                            ps.looplen = ps.endpoint;
                            ps.loopstart = 0;

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
            else // .AKI file (my custom format)
            {
                // File format: (little-endian storage format, LSB then MSB)
				// 1) 4 byte unsigned int, magic number to identify type of file
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
                  return;
                }

                printm("Read " + String(iBytesRead) + " bytes");

                unsigned __int32 my_magic;

                memcpy(&my_magic, &fileBuf[0], UINT32SIZE);
                //    printm("magic = " + String(my_magic));

                if (my_magic != MAGIC_NUM_AKI)
                {
                  printm("File is not the right kind!");
                  return;
                }

                // load ps (the sample-header info) from fileBuffer
                memcpy(&ps, &fileBuf[0+UINT32SIZE], AKI_FILE_HEADER_SIZE);
                ps.name[MAX_NAME_S900] = '\0';
                trimright(ps.name);

                // encode new sample name if any entered
                //    char name[11] = "TEMPNAME00";
                //    sprintf(ps.name, "%.*s", MAX_NAME_S900, name);

                // find the sample's index on the target machine
                if ((sampIndex = FindIndex(ps.name)) < 0)
                    return;

                // get the +/- shift_count before we limit ps.bits_per_word (below)
                // (should either be 16-16 = 0, 14-14 = 0, 14-16 = -2 or 16-14 = +2
                // the only case we care about is +2 because we will have to
                // down-convert by right-shifting 2 to target an S900 with
                // a 16-bit sample saved from an S950)
                int shift_count = ps.bits_per_word-machine_max_bits_per_word;

                // 2 bytes 14-bits S900 or 3 bytes 16-bits S950
				// If the .aki file has 16-bit samples and we are sending to
                // an S900, we need to lower the PSTOR bits_per_word to 14!
                if (ps.bits_per_word > machine_max_bits_per_word)
                {
                    ps.bits_per_word = (unsigned __int16)machine_max_bits_per_word;
                    printm("reduced bits per word in .aki file to fit the S900 (14-bits max)!");
                    printm("(if you are sending to the S950 select \"Target S950\" in the menu!)");
                }

                // encode samp_hedr and samp_parms arrays
                encode_sample_info(sampIndex, &ps);

                print_ps_info(&ps);

                // request common reception enable
                exmit(0, SECRE, true);

                // transmit sample header info
                comws(HEDRSIZ, samp_hedr, true);

                // wait for acknowledge
                if (get_ack(0))
                    return;

                __int16 baseline = (__int16)(1 << (ps.bits_per_word-1));

                unsigned __int16 max_val = (unsigned __int16)((1 << ps.bits_per_word)-1);

                __int16 *ptr = (__int16*)&fileBuf[AKI_FILE_HEADER_SIZE + UINT32SIZE];
                int ReadCounter = AKI_FILE_HEADER_SIZE + UINT32SIZE; // We already processed the header

                blockct = 0;

                // init progress display
                int divisor = (ps.totalct <= 19200) ? 8 : 32;
                String dots;

                for(;;)
                {
                    // End of file?
                    if (ReadCounter >= iBytesRead)
                        break;

                    // send block #
                    Byte temp = (Byte)(blockct & 0x7f);
                    comws(1, &temp, false); // no delay

                    Byte checksum = 0;

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
                            val >>= shift_count-1;

                            bool bRoundUp = val & 1; // need to round up?

                            // discard msb of discarded bits...
                            val >>= 1;

                            if (bRoundUp && val != max_val)
                                val++;

                        }

                        xmit(val, bytes_per_word, ps.bits_per_word, checksum);
                    }

                    comws(1, &checksum, false); // send checksum (no delay)

                    // do the ..... progress indicator
                    if (blockct % divisor == 0)
                    {
                        dots += ".";
                        printm(dots);
                    }

                    // wait for acknowledge
                    if (get_ack(blockct+1) != 0)
                    {
                        // sample send failed!

                        unsigned int countSent = blockct*words_per_block;
						printm("sent " + String(countSent) + " of " +
                                         String(ps.totalct) + " sample words!");

                        // limits
                        if (countSent < ps.totalct)
                        {
                            ps.totalct = countSent;
                            if (ps.endpoint > ps.totalct-1)
                                ps.endpoint = ps.totalct-1;
                            if (ps.looplen > ps.endpoint)
                                ps.looplen = ps.endpoint;
                            if (ps.loopstart > ps.endpoint-ps.looplen)
                                ps.loopstart = ps.endpoint-ps.looplen;

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

            Byte temp = EEX;
            comws(1, &temp, false); // no delay

            exmit(0, SECRD, true); // request common reception disable

            if (m_auto_rename)
            {
                // look up new sample in catalog, when you write a new sample it
                // shows up as "00", "01", "02"

                char locstr[3];
                sprintf(locstr, "%02d", sampIndex);
                sampIndex = findidx(locstr);

                // returns the 0-based sample index if a match is found
                // -1 = error
                // -2 = no samples on machine
                // -3 = samples on machine, but no match
                if (sampIndex == -1)
                {
                    printm("catalog search error for: \"" + String(locstr) + "\"");
                    return; // catalog error
				}

                if (sampIndex == -2)
                    sampIndex = 0; // we will be the only sample...

                if (sampIndex < 0)
                {
                    printm("index string \"" + String(locstr) + "\" not found!");
                    return;
                }

                send_samp_parms(sampIndex);

                if (!bSendAborted)
                    printm("sample written ok! (index=" + String(sampIndex)+ ")");
            }
            else
            {
                if (!bSendAborted)
                    printm("sample written ok!");
            }
        }
        catch(...)
        {
            printm("can't load file: \"" + sFilePath + "\"");
        }
    }
    __finally
    {
        if (fileBuf != NULL)
            delete [] fileBuf;

        if (iFileHandle)
            FileClose(iFileHandle);

        m_systemBusy = false;
    }
}
//---------------------------------------------------------------------------
// tricky algorithm I came up with (the inverse of the one in RxSamp.cpp)
// builds and sends only the required number of 7-bit bytes representing one
// sample-word to the S900 or S950 - S.S.
//
// checksum in/out is by-reference
void __fastcall TFormS900::xmit(unsigned __int16 val, int bytes_per_word,
                               int bits_per_word, Byte &checksum)
{
    for (int ii = 1; ii <= bytes_per_word; ii++)
    {
		int shift_count = bits_per_word - (ii*7);

        Byte out_val;

        if (shift_count >= 0)
            out_val = (Byte)(val >> shift_count);
        else
            out_val = (Byte)(val << -shift_count);

        out_val &= 0x7f; // mask msb to 0

        checksum ^= out_val;
        comws(1, &out_val, false); // no delay
    }
}
//---------------------------------------------------------------------------
// puts PSTOR struct (ps) info into samp_parms[] and samp_hedr[] arrays
void __fastcall TFormS900::encode_sample_info(int samp, PSTOR* ps)
{
    //
    // SAMPLE PARAMETERS 129 bytes (do this before encoding header)
    //

    samp_parms[0] = BEX;
    samp_parms[1] = AKAI_ID;
    samp_parms[2] = 0; // midi chan
    samp_parms[3] = SPRM;
    samp_parms[4] = S900_ID;
    samp_parms[5] = (Byte)(samp);
    samp_parms[6] = 0; // reserved

    // copy ASCII sample name, pad with blanks and terminate
    char locstr[MAX_NAME_S900+1];
    strncpy((char*)locstr, (char*)ps->name, MAX_NAME_S900);
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
    // SAMPLE HEADER, 19 bytes
    //

    // encode excl,syscomid,sampdump
    samp_hedr[0] = (Byte)0xf0;
    samp_hedr[1] = (Byte)0x7e;
    samp_hedr[2] = (Byte)0x01;
    samp_hedr[3] = (Byte)samp;
    samp_hedr[4] = 0; // MSB samp idx always 0 for S900

    // bits per word
    samp_hedr[5] = (Byte)ps->bits_per_word;

    // sampling period
    encode_hedrTB(ps->period, &samp_hedr[6]); // 3

    // number of sample words
    encode_hedrTB(ps->totalct, &samp_hedr[9]); // 3

    // loop start point
    encode_hedrTB(ps->endpoint-ps->looplen, &samp_hedr[12]); // 3

    // loop end, S900 takes this as the end point
    encode_hedrTB(ps->endpoint, &samp_hedr[15]); // 3

    // use ps->looping mode 'A', 'L' or 'O' (alternating, looping or one-shot)
    // to set samp_hedr[18], loop mode: 0=looping, 1=alternating
    samp_hedr[18] = (Byte)((ps->loopmode == 'A') ? 1 : 0);
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::send_samp_parms(unsigned int index)
{
    // transmit sample parameters
    samp_parms[5] = (Byte)(index);
    comws(PARMSIZ, samp_parms, true);
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::encode_parmsDB(Byte c, Byte* dest)
{
    *dest++ = (c & (Byte)0x7f);
    *dest = (Byte)((c & (Byte)0x80) ? 1 : 0);
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::encode_parmsDB(Byte* source, Byte* dest, int numchars)
{
    for (int ii = 0 ; ii < numchars ; ii++)
	{
        *dest++ = (*source & (Byte)0x7f);
        *dest++ = (Byte)((*source & (Byte)0x80) ? 1 : 0);
        source++;
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::encode_parmsDD(unsigned __int32 value, Byte* tp)
{
    for (int ii = 0 ; ii < 4 ; ii++)
    {
        *tp++ = (Byte)(value & 0x7f);
        value >>= 7;
        *tp++ = (Byte)(value & 1);
        value >>= 1;
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::encode_parmsDW(unsigned __int32 value, Byte* tp)
{
    for (int ii = 0 ; ii < 2 ; ii++)
    {
      *tp++ = (Byte)(value & 0x7f);
      value >>= 7;
      *tp++ = (Byte)(value & 1);
      value >>= 1;
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::encode_hedrTB(unsigned __int32 value, Byte* tp)
{
    *tp++ = (Byte)(value & 0x7f);
    value >>= 7;
    *tp++ = (Byte)(value & 0x7f);
    value >>= 7;
    *tp = (Byte)(value & 0x7f);
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::compute_checksum(int min_index, int max_index)
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
void __fastcall TFormS900::catalog(bool print)
{
    // 7 hedr bytes + chksum byte + EEX = 9...
    if (m_byteCount < 9 || TempArray[3] != DCAT)
    {
        printm("check cable, is sampler on and configured\r\n"
          "for RS232, 38400 baud?");
        return;
    }

    int entries = (m_byteCount-9)/sizeof(S900CAT);

    if (!entries)
    {
        printm("No Samples or programs in S900");
        return;
    }

    S900CAT * tempptr = (S900CAT *)&TempArray[7]; // Skip header
    CAT * permsampptr = (CAT *)&PermSampArray[0];
    CAT * permprogptr = (CAT *)&PermProgArray[0];

    m_numSampEntries = 0;
    m_numProgEntries = 0;

    for (int ii = 0 ; ii < entries ; ii++)
    {
        if (tempptr->type == 'S')
        {
            if (m_numSampEntries < MAX_SAMPS)
            {
                sprintf(permsampptr->name, "%.*s", MAX_NAME_S900, tempptr->name);
                trimright(permsampptr->name);

                permsampptr->sampidx = tempptr->sampidx;

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

                permprogptr->sampidx = tempptr->sampidx;

                m_numProgEntries++; // increment counter
                permprogptr++; // next structure
            }
        }

        tempptr++; // next structure
    }


    if (print)
    {
        permsampptr = (CAT *)&PermSampArray[0];
        permprogptr = (CAT *)&PermProgArray[0];

        printm("Programs:");

        for (int ii = 0 ; ii < m_numProgEntries ; ii++)
        {
            printm(String(permprogptr->sampidx) + ":\"" + String(permprogptr->name) + "\"");
            permprogptr++;
        }

        printm("Samples:");

        for (int ii = 0 ; ii < m_numSampEntries ; ii++)
        {
            printm(String(permsampptr->sampidx) + ":\"" + String(permsampptr->name) + "\"");
            permsampptr++;
        }
    }
}
//---------------------------------------------------------------------------
// Receive data routines
//---------------------------------------------------------------------------
int __fastcall TFormS900::receive(int count)
// set count to 0 to receive a complete message
// set to "count" to receive a partial message.
{
  Byte tempc;
  bool have_bex = false;

  this->m_byteCount = 0;
  this->m_rxTimeout = false;

  Timer1->Interval = ACKINITIALTIMEOUT; // 4 seconds
  Timer1->OnTimer = Timer1RxTimeout; // set handler
  Timer1->Enabled = true; // start timeout timer

  try
  {
    for(;;)
    {
      if (ApdComPort1->CharReady())
      {
        // keep ressetting timer to hold off timeout
        Timer1->Enabled = false;
        Timer1->Interval = ACKTIMEOUT;
        Timer1->Enabled = true;

        tempc = ApdComPort1->GetChar();

        if (!have_bex)
        {
          if (tempc == BEX || count != 0)
          {
            have_bex = true;
            TempArray[m_byteCount++] = tempc;
          }
        }
        else
        {
          TempArray[m_byteCount++] = tempc;

          if (count)
          {
            if (m_byteCount >= count)
              return 0;
          }
          else if (tempc == EEX)
            return 0;

          if (m_byteCount >= (int)TEMPCATBUFSIZ) // at buffer capacity... error
            return 2;
        }
      }
      else
      {
        if(m_rxTimeout)
          break;

        Application->ProcessMessages(); // need this to detect the timeout
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
// returns 0 if acknowledge received ok
int __fastcall TFormS900::get_ack(int blockct)
{
    if (receive(0))
    {
        printm("timeout receiving acknowledge (ACK)! (block=" + String(blockct) + ")");
        return 1;
    }

    if (m_byteCount == ACKSIZ)
    {
        Byte c = TempArray[m_byteCount-2];

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

        printm("bad acknowledge, unknown code! (block=" + String(blockct) + ", code=" + String((int)(unsigned int)c) + ")");
        return 4;
    }

    printm("bad acknowledge, wrong size! (block=" + String(blockct) + ", size=" + String(m_byteCount) + ")");
    return 5;
}
//---------------------------------------------------------------------------
// Misc methods
//---------------------------------------------------------------------------
void __fastcall TFormS900::exmit(int samp, int mode, bool bDelay)
{

    Byte midistr[8];

    midistr[0] = BEX;
    midistr[1] = AKAI_ID;
    midistr[2] = 0;
    midistr[3] = (Byte)mode;
    midistr[4] = S900_ID;
    midistr[5] = (Byte)samp;
    midistr[6] = 0;
    midistr[7] = EEX;

    comws(8, midistr, bDelay);
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::cxmit(int samp, int mode, bool bDelay)
{
    Byte midistr[6];

    midistr[0] = BEX;
    midistr[1] = COMMON_ID;
    midistr[2] = (Byte)mode;
    midistr[3] = (Byte)samp;
    midistr[4] = 0;
    midistr[5] = EEX;

    comws(6, midistr, bDelay);
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::chandshake(int mode)
{
    Byte midistr[4];

    midistr[0] = BEX;
    midistr[1] = COMMON_ID;
    midistr[2] = (Byte)mode;
    midistr[3] = EEX;

    comws(4, midistr, false); // no delay
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::comws(int count, Byte* ptr, bool bDelay)
{
    // this delay is necessary to give the older-technology S900/S950 time
    // to digest any previous commands such as sending a program, a sample
    // data-block, request sysex on, catalog, etc. Without it, all programs
    // may not get assimilated by the target-machine (for certain!)
    if (bDelay)
		DelayGpTimer(25); // add some space between transmits

    for (int ii = 0 ; ii < count ; ii++)
    {
        while (ApdComPort1->OutBuffFree < 1);
        ApdComPort1->PutChar(*ptr++);
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::printm(String message)
{
    Memo1->Lines->Add(message);
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::print_ps_info(PSTOR* ps)
{
    FormS900->printm("");
    FormS900->printm("Output Parameters:");
    FormS900->printm("sample length (in words): " + String(ps->totalct));
    FormS900->printm("end point: " + String(ps->endpoint));
    FormS900->printm("frequency (hertz): " + String(ps->freq));
    FormS900->printm("pitch: " + String(ps->pitch));
    FormS900->printm("period (nanoseconds): " + String((unsigned int)ps->period));
    FormS900->printm("bits per word: " + String(ps->bits_per_word));
    FormS900->printm("loop start point: " + String(ps->loopstart));
    FormS900->printm("loop length: " + String(ps->looplen));

    if (ps->flags & (Byte)1)
      FormS900->printm("velocity crossfade: on");
    else
      FormS900->printm("velocity crossfade: off");

    if (ps->flags & (Byte)2)
      FormS900->printm("reverse waveform: yes");
    else
      FormS900->printm("reverse waveform: no");

    if (ps->loopmode == 'O')
      FormS900->printm("looping mode: one-shot");
    else if (ps->loopmode == 'L')
      FormS900->printm("looping mode: looping");
    else if (ps->loopmode == 'A')
      FormS900->printm("looping mode: alternating");
    else
      FormS900->printm("looping mode: unknown");

    FormS900->printm("sample name: \"" + String(ps->name) + "\""); // show any spaces
}
//---------------------------------------------------------------------------
int __fastcall TFormS900::findidx(char* sampName)
// returns the 0-based sample index if a match is found
// -1 = error
// -2 = no samples on machine
// -3 = samples on machine, but no match
//
// ignores spaces to right of name in comparison
{
    // Request S900 Catalog
    exmit(0, RCAT, true);

    if (receive(0))
        return -1;

    // sets m_numSampEntries...
    // NOTE: the program/sample names are all right-trimmed by catalog().
    catalog(false); // populate sample and program structs (no printout)

    if (m_numSampEntries == 0)
        return -2; // no samples on machine

    CAT* catptr = (CAT*)PermSampArray;

    // make a right-trimmed copy of name
    char newName[MAX_NAME_S900+1];
    strncpy(newName, sampName, MAX_NAME_S900);
    newName[MAX_NAME_S900] = '\0';
    trimright(newName);

    for (int ii = 0 ; ii < m_numSampEntries ; ii++)
    {
        if (strncmp(catptr->name, newName, MAX_NAME_S900) == 0) // names match?
            return catptr->sampidx;

        catptr++;
    }

    return -3;
}
//---------------------------------------------------------------------------
int __fastcall TFormS900::FindIndex(char* pName)
{
    // now we need to set "samp index"
    int sampIndex = findidx(pName);

    // retval = the 0-based sample index if a match is found
    // -1 = error
    // -2 = no samples on machine
    // -3 = samples on machine, but no match
	if (sampIndex == -1)
    {
        printm("catalog search error for: \"" + String(pName).TrimRight() + "\"");
        printm("check RS232 connection and/or baud-rate: " + String(ApdComPort1->Baud));
    }
    else if (sampIndex >= 0) // sample we are about to write is already on machine
        printm("found sample \"" + String(pName).TrimRight() + "\" at index " + String(sampIndex));
    else if (sampIndex == -3) // samples on machine, but not the one we are about to write
        sampIndex = m_numSampEntries;
    else // no samples on machine
        sampIndex = 0;

    return sampIndex;
}
//---------------------------------------------------------------------------
String __fastcall TFormS900::GetFileName(void)
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
__int32 __fastcall TFormS900::FindSubsection(Byte* &fileBuffer,
                                          char* chunkName, UINT fileLength)
{
  try
  {
    // bypass the first 12-bytes "RIFFnnnnWAVE" at the file's beginning...
    Byte* chunkPtr = fileBuffer+12;

    Byte* pMax = fileBuffer+fileLength;

    int chunkLength;

    // Search file for named chunk
    for(;;)
    {
      // Chunks are 4-bytes ANSI followed by a 4-byte length, lsb-to-msb
      if (chunkPtr+8 >= pMax)
        return -2;

      chunkLength = *(__int32*)(chunkPtr+4);

      // look for the 4-byte chunk-name
      if (StrCmpCaseInsens((char*)chunkPtr, (char*)chunkName, 4))
      {
        fileBuffer = chunkPtr+8; // return pointer to data, by reference
        return chunkLength;
      }

      chunkPtr += chunkLength+8;
    }
  }
  catch(...)
  {
    return -1;
  }
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::MenuPutSampleClick(
      TObject *Sender)
{
    String filePath = GetFileName();

    if (!filePath.IsEmpty())
        PutSample(filePath);

    m_DragDropFilePath = "";
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::MenuGetCatalogClick(TObject *Sender)
{
    if (m_systemBusy) return;

    try
	{
        m_systemBusy = true;

        ListBox1->Clear();
        Memo1->Clear();
        ApdComPort1->FlushInBuffer();
        ApdComPort1->FlushOutBuffer();

        // Request S900 Catalog
        exmit(0, RCAT, false);

        if (receive(0))
        {
            printm("timeout receiving catalog!");
            return;
        }

        catalog(true);
    }
    __finally
    {
        m_systemBusy = false;
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::MenuGetSampleClick(TObject *Sender)
{
    if (m_systemBusy) return;

    try
    {
        m_systemBusy = true;

        try
        {
            ListBox1->Clear();
            Memo1->Clear();
            ApdComPort1->FlushInBuffer();
            ApdComPort1->FlushOutBuffer();

            // Request S900 Catalog
            exmit(0, RCAT, false);

            if (receive(0))
            {
                printm("timeout receiving catalog!");
                return;
            }

			catalog(false); // sets m_numSampEntries

            if (!m_numSampEntries)
            {
                printm("no samples in machine!");
                return;
            }

            CAT *catp = (CAT *)PermSampArray;

            for (int ii = 0 ; ii < m_numSampEntries ; ii++, catp++)
                ListBox1->Items->Add(catp->name);

            printm("\r\n<--- ***Click a sample at the left to receive\r\n"
                               "and save it to a .AKI file***");
        }
        catch(...)
        {
            ShowMessage("Can't get catalog");
        }
    }
    __finally
    {
        m_systemBusy = false;
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::ListBox1Click(TObject *Sender)
{
    if (m_systemBusy) return;

    try
    {
        m_systemBusy = true;

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
            SaveDialog1->FileName = ExtractFilePath(SaveDialog1->FileName) +
									ListBox1->Items->Strings[ListBox1->ItemIndex];

            SaveDialog1->FileName.TrimRight();

            if (SaveDialog1->Execute())
            {
                ListBox1->Repaint();
                ApdComPort1->FlushInBuffer();
                ApdComPort1->FlushOutBuffer();

                if (GetSample(ListBox1->ItemIndex, SaveDialog1->FileName))
                    printm("not able to save sample!");
                else
                    printm("sample saved as: \"" + SaveDialog1->FileName + "\"");
            }
        }
        catch(...)
        {
            printm("error, can't save file: \"" + SaveDialog1->FileName + "\"");
        }
    }
    __finally
    {
        m_systemBusy = false;
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::Help1Click(TObject *Sender)
{
    Memo1->Lines->Clear();
    printm("Connect a cable between the S900's (or S950) RS232\r\n"
      "PORT and COM1 (or other port) on your computer. This is an ordinary\r\n"
      "DB25-male to DB9-female null-modem cable. If your computer has no\r\n"
      "connector, you will need a USB-to-RS232 adaptor.\r\n\r\n"
      "On the S900/S950 push the MIDI button.\r\n"
      "Push the DOWN button and scroll to menu 5.\r\n"
      "Push the RIGHT button and select \"2\" control by RS232.\r\n"
      "Push the RIGHT button again and enter \"3840\".\r\n"
      "This will set the machine to 38400 baud.\r\n\r\n"
	  "To test, select \"Get list of samples and programs\" from the menu...\r\n"
	  "A box may appear asking for the com port. Select a port and click OK.\r\n"
	  "You should see the samples and programs listed in this window.\r\n"
	  "      - Cheers, Scott Swift dxzl@live.com");
}
//---------------------------------------------------------------------------
//void __fastcall TFormS900::MenuTargetS950Click(
//      TObject *Sender)
//{
//  MenuTargetS950->Checked = !MenuTargetS950->Checked;
//  m_target_S950 = MenuTargetS950->Checked;
//}
//---------------------------------------------------------------------------
//void __fastcall TFormS900::MenuUseSmoothQuantizationClick(
//      TObject *Sender)
//{
//  MenuUseSmoothQuantization->Checked = !MenuUseSmoothQuantization->Checked;
//  m_use_smooth_quantization = MenuUseSmoothQuantization->Checked;
//}
//---------------------------------------------------------------------------
void __fastcall TFormS900::MenuUseRightChanForStereoSamplesClick(TObject *Sender)
{
  MenuUseRightChanForStereoSamples->Checked = !MenuUseRightChanForStereoSamples->Checked;
  m_use_right_chan = MenuUseRightChanForStereoSamples->Checked;
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::MenuAutomaticallyRenameSampleClick(
      TObject *Sender)
{
  MenuAutomaticallyRenameSample->Checked = !MenuAutomaticallyRenameSample->Checked;
  m_auto_rename = MenuAutomaticallyRenameSample->Checked;
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::MenuUseHWFlowControlBelow50000BaudClick(
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
void __fastcall TFormS900::MenuGetProgramsClick(TObject *Sender)
{
    if (m_systemBusy) return;

    int iFileHandle = 0;
    Byte* buf = NULL;
    bool bError = false;

    try
    {
        m_systemBusy = true;

        ListBox1->Clear();
        Memo1->Clear();
        ApdComPort1->FlushInBuffer();
        ApdComPort1->FlushOutBuffer();

        SaveDialog1->Title = "Save all programs to .pgm file...";
        SaveDialog1->DefaultExt = "pgm";
        SaveDialog1->Filter = "Programs (*.pgm)|*.pgm|"
                   "All files (*.*)|*.*";
        SaveDialog1->FilterIndex = 2; // start the dialog showing all files
        SaveDialog1->Options.Clear();
        SaveDialog1->Options << ofHideReadOnly
             << ofPathMustExist << ofOverwritePrompt << ofEnableSizing
                << ofNoReadOnlyReturn;

        // Use the sample-name in the list as the file-name
        SaveDialog1->FileName = ExtractFilePath(SaveDialog1->FileName) + "akai_progs";

        if (!SaveDialog1->Execute())
            return;

        SaveDialog1->FileName.TrimRight();

        if (!FileExists(SaveDialog1->FileName))
            iFileHandle = FileCreate(SaveDialog1->FileName);
        else
            iFileHandle = FileOpen(SaveDialog1->FileName, fmShareDenyNone | fmOpenReadWrite);

        if (iFileHandle == 0)
        {
            printm("can't open file to write: \"" + SaveDialog1->FileName + "\"");
            return;
        }

        m_numProgEntries = 0; // need this in __finally

        try
		{
            // allocate memory for largest program with up to 64 keygroups
            // a single program has PRG_FILE_HEADER_SIZE + (X*PROGKEYGROUPSIZ) + 2 for
            // checksum and EEX bytes
            buf = new Byte[PRG_FILE_HEADER_SIZE + (MAX_KEYGROUPS*PROGKEYGROUPSIZ) + 2];

            if (buf == NULL)
            {
                printm("can't allocate buffer for program and keygroups!");
                bError = true;
                return;
            }

            // Request S900 Catalog
            exmit(0, RCAT, false);

            if (receive(0))
            {
                printm("timeout receiving catalog!");
                bError = true;
                return;
            }

            catalog(false); // sets m_numProgEntries

            if (!m_numProgEntries)
            {
                printm("no programs in machine!");
                bError = true;
                return;
            }

            printm("reading " + String(m_numProgEntries) + " programs from S900/S950...");

            __int32 totalBytesWritten = 0;
            __int32 bytesWritten;

            // write the magic number
            unsigned __int32 my_magic = MAGIC_NUM_PRG; // uniquely identifies a .prg file
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

            Byte* bufptr;
            String dots;

            // request each program and add it to file on the fly
            for (int ii = 0; ii < m_numProgEntries; ii++)
            {
                Application->ProcessMessages();

                // request next program and its keygroups
                exmit(ii, RPRGM, true);

                // get program header from serial port into TempArray
                if (receive(PRG_FILE_HEADER_SIZE))
                {
                    printm("timeout receiving programs header!");
                    bError = true;
                    return;
                }

                if (TempArray[3] != PRGM || TempArray[4] != S900_ID)
                {
                    printm("invalid programs header! (1)");
                    bError = true;
                    return;
                }

                // 1/12/2017 - here, we should get the same index we requested
                // back, but S950 seems to send 0 back all the time, so force it!
                if (TempArray[5] != ii)
                {
                    TempArray[5] = (Byte)ii;
                    printm("(forced program index to: " + String(ii) + ")");
                }

                bufptr = buf; // back to beginning

                // write header to buf
                memcpy(bufptr, TempArray, PRG_FILE_HEADER_SIZE);
				bufptr += PRG_FILE_HEADER_SIZE;

                // get number of keygroups in one program (1-31)
                int numKeygroups = decode_parmsDB(&TempArray[53]); // 53, 54

                if (!numKeygroups)
                {
                    printm("program " + String(ii) + " has no keygroups!");
                    continue;
                }

                printm("program " + String(ii) + " has " +
                                String(numKeygroups) + " keygroups");

                // limit
                if (numKeygroups > MAX_KEYGROUPS)
                    numKeygroups = MAX_KEYGROUPS;

                for (int jj = 0; jj < numKeygroups; jj++)
                {
                    // get keygroup from serial port into TempArray
                    if (receive(PROGKEYGROUPSIZ))
                    {
                        printm("timeout receiving keygroup " + String(jj) +
                                        " for program " + String(ii) + "!");
                        bError = true;
                        return;
                    }

                    // write keygroup to buf
                    memcpy(bufptr, TempArray, PROGKEYGROUPSIZ);
                    bufptr += PROGKEYGROUPSIZ;
                }

                // get checksum and EEX from serial port into TempArray
                if (receive(2))
                {
                    printm("timeout receiving checksum and EEX!");
                    bError = true;
                    return;
                }

                if (TempArray[1] != EEX)
                {
                    printm("expected EEX, got: " + String((int)TempArray[1]));
                    bError = true;
                    return;
                }

				// write checksum and EEX to buf
                memcpy(bufptr, TempArray, 2);
                bufptr += 2;

                // size of this program
                __int32 progSize = (int)(bufptr - buf);

                // compute checksum
                Byte checksum = 0;
                for (int jj = 7;  jj < progSize-2; jj++)
                    checksum ^= buf[jj];

                if (buf[progSize-2] != checksum)
                {
                    printm("bad checksum for program " + String(ii) + "!");
                    bError = true;
                    return;
                }

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

                // copy program in buf to file
                totalBytesWritten += progSize;
                bytesWritten = FileWrite(iFileHandle, buf, progSize);

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
        catch(...)
        {
            printm("exception thrown while receiving programs!");
            bError = true;
        }
    }
    __finally
    {
        if (iFileHandle)
          FileClose(iFileHandle);

        if (buf != NULL)
          delete [] buf;

        if (!bError)
            printm(String(m_numProgEntries) + " programs successfully saved!");
        else
        {
            try { DeleteFile(SaveDialog1->FileName); } catch(...) {}
            printm("unable to save programs...");
        }

        m_systemBusy = false;
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
void __fastcall TFormS900::MenuPutProgramsClick(TObject *Sender)
{
    if (m_systemBusy) return;

    bool bError = false;
    Byte* buf = NULL;
    int iFileHandle = 0;
    __int32 numProgs = 0; // need this in __finally

    try
    {
        m_systemBusy = true;

        Memo1->Clear();

        ApdComPort1->FlushInBuffer();
        ApdComPort1->FlushOutBuffer();

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

        OpenDialog1->FileName.TrimRight();

        iFileHandle = FileOpen(OpenDialog1->FileName, fmShareDenyNone | fmOpenRead);

        if (iFileHandle == 0)
        {
            printm("can't open file to read: \"" + OpenDialog1->FileName + "\"");
            return;
        }

        try
        {
            __int32 iFileLength = FileSeek(iFileHandle,0,2); // seek to end

            // seek/read the stored file-length (__int32 at end of the file)
            FileSeek(iFileHandle, iFileLength-UINT32SIZE, 0);
            __int32 storedFileLength;
            int bytesRead = FileRead(iFileHandle, &storedFileLength, UINT32SIZE);

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

            FileSeek(iFileHandle,0,0); // back to start

            // read the magic number
            unsigned __int32 my_magic; // uniquely identifies a .prg file
            bytesRead = FileRead(iFileHandle, &my_magic, UINT32SIZE);

            if (bytesRead != UINT32SIZE)
            {
                printm("file is corrupt or not the right kind...");
                bError = true;
                return;
            }

            if (my_magic != MAGIC_NUM_PRG)
            {
                printm("file is not a .prg programs file for the S900/S950!");
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

            printm("sending " + String(numProgs) + " programs to S900/S950...");

            // allocate memory for largest program with up to 64 keygroups
            // a single program has PRG_FILE_HEADER_SIZE + (X*PROGKEYGROUPSIZ) + 2 for
            // checksum and EEX bytes
            int bufSize = PRG_FILE_HEADER_SIZE + (MAX_KEYGROUPS*PROGKEYGROUPSIZ) + 2;
            buf = new Byte[bufSize];

            if (buf == NULL)
            {
                printm("can't allocate buffer for program and keygroups!");
                bError = true;
                return;
            }

            String dots; // progress display

            // request each program and add it to file on the fly
            for (int ii = 0; ii < numProgs; ii++)
            {
                Application->ProcessMessages();

                // read program-size from file.
                // a program in the file is already formatted for the S900 and
                // includes the BEX, checksum and EEX.
                __int32 progSize;
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

                if (buf[progSize-1] != EEX)
                {
                    printm("buffer does not contain EEX! (index=" + String(ii) + ")");
                    bError = true;
                    return;
                }

                // send program to Akai S950/S900
                comws(progSize, buf, false);

                dots += '.';
                printm(dots);

                // Must delay 100ms or programs won't be transmitted!
                DelayGpTimer(DELAY_BETWEEN_PROGRAMS);
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
          delete [] buf;

        if (!bError)
            printm(String(numProgs) + " programs successfully sent!");
        else
            printm("unable to write programs...");

        m_systemBusy = false;
	}
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::WMDropFile(TWMDropFiles &Msg)
{
  if (m_systemBusy) return;

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
            Timer1->Interval = 50;
            Timer1->OnTimer = Timer1FileDropTimeout; // set handler
            Timer1->Enabled = true; // fire event to send file
          }
      }
    }
  }
  catch(...){}
}
//---------------------------------------------------------------------------
// Warning: To prevent recursive anomalies, need to do while(SystemBusy);
// at the start of any event that uses DelayGpTimer!
void __fastcall TFormS900::DelayGpTimer(int time)
{
    StartGpTimer(time);
    while (!IsGpTimeout())
        Application->ProcessMessages();
    StopGpTimer();
}
//---------------------------------------------------------------------------
bool __fastcall TFormS900::IsGpTimeout(void)
{
    return m_gpTimeout;
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::StopGpTimer(void)
{
    Timer1->Enabled = false;
    Timer1->OnTimer = NULL;
    m_gpTimeout = false;
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::StartGpTimer(int time)
{
    Timer1->Enabled = false;
    Timer1->OnTimer = Timer1GpTimeout;
    Timer1->Interval = time;
    Timer1->Enabled = true;
    m_gpTimeout = false;
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::Timer1GpTimeout(TObject *Sender)
{
    // used for midi-diagnostic function
    Timer1->Enabled = false;
    m_gpTimeout = true;
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::Timer1FileDropTimeout(TObject *Sender)
{
    Timer1->Enabled = false;

    if (!m_DragDropFilePath.IsEmpty())
    {
        PutSample(m_DragDropFilePath);
        m_DragDropFilePath = "";
    }
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::Timer1RxTimeout(TObject *Sender)
{
    Timer1->Enabled = false;
    m_rxTimeout = true;
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::ComboBox1Change(TObject *Sender)
{
// Added SetComPort() 10/9/16
//  ApdComPort1->Baud = ComboBox1->Text.ToIntDef(38400);
  SetComPort(ComboBox1->Text.ToIntDef(38400));
//  Memo1->SetFocus();
}
//---------------------------------------------------------------------------
void __fastcall TFormS900::SetComPort(int baud)
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
void __fastcall TFormS900::trimright(char* pStr)
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
bool __fastcall TFormS900::StrCmpCaseInsens(char* sA, char* sB, int len)
{
  return String(sA, len).LowerCase() == String(sB, len).LowerCase();
}
//---------------------------------------------------------------------------

