object FormS900: TFormS900
  Left = 0
  Top = 0
  Caption = 'AKAI S900/S950 Sampler RS232 Communications'
  ClientHeight = 126
  ClientWidth = 501
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -11
  Font.Name = 'Tahoma'
  Font.Style = []
  Icon.Data = {
    000001000200101010000000000028010000260000002020100000000000E802
    00004E0100002800000010000000200000000100040000000000C00000000000
    0000000000000000000000000000000000000000800000800000008080008000
    00008000800080800000C0C0C000808080000000FF0000FF000000FFFF00FF00
    0000FF00FF00FFFF0000FFFFFF00999AAACCCBBB5555999AAACCCBBB5555999A
    AACCCBBB5555999AAACCCBBB5555999AAACCCBBB5555999AAACCCBBB5555999A
    AACCCBBB5555999AAACCCBBB5555999AAACCCBBB5555999AAACCCBBB5555999A
    AACCCBBB5555999AAACCCBBB5555999AAACCCBBB5555999AAACCCBBB5555999A
    AACCCBBB5555999AAACCCBBB5555000000000000000000000000000000000000
    0000000000000000000000000000000000000000000000000000000000000000
    0000000000000000000000000000280000002000000040000000010004000000
    0000800200000000000000000000000000000000000000000000000080000080
    000000808000800000008000800080800000C0C0C000808080000000FF0000FF
    000000FFFF00FF000000FF00FF00FFFF0000FFFFFF00CCCCDDDDDD0099990000
    000000444400CCCCCDDDDD0009999900000004000040CCCCCCCDDD000A999999
    000040000004CC55CCCCDD000AAA9999900040000004CC55CCCCDDD00AAAA999
    990040000004CC5555CCCCD00AAAAAA9999040000004CCC5555CCCC000AAAAAA
    999904000040CCC55555CCC000AAAAAAA999004444000CC5555555CC00AAAAAA
    A999000000000CC5555555CC00AAAAAAA999000000000CCC5555555CC00AAAAA
    A999000000000CCC5555555CC00AAAAA99900000000000CC5555555CC00AAAA9
    99900000000000CC55555555CC0AAAA999000000000000CCC5555555CC00AA99
    90000000000000CCC5555555CC00AA99000000000000000CC5555555CC00A999
    000000000000000CC5555555CC000999000000000000000CC5555555CC000999
    000000000000000CCC55555CCC000B99000000000000000CCC55555CC2000B99
    9000000000000000CC55555CC2000BB99900000000000000CC5555CCC2200BB9
    9990000000000000CCC555CCC22000BB9999000000000000CCC55CCC222000BB
    B9999000000000000CC5CCCC222200BBBBB99990000000000CCCCCC2222200BB
    BBBB9999900000000CCCCC222222000BBBBBBB999900000000CCC00000000000
    0000000000000000000000000000000000000000000000000000000000000000
    000000000000000000000000000000000000000000000000FFC300003F910000
    0F46000007420000035A00000142000000BD000000C3800000FF800000FF8000
    00FF800001FFC00001FFC00003FFC00007FFC0000FFFE0000FFFE0000FFFE000
    0FFFE0000FFFE00007FFF00003FFF00001FFF00000FFF000007FF800001FF800
    0007F8000003F8000003F8000003FFFFFFFFFFFFFFFF}
  Menu = MainMenu1
  OldCreateOrder = False
  Position = poDesktopCenter
  OnClose = FormClose
  OnCreate = FormCreate
  PixelsPerInch = 96
  TextHeight = 13
  object Panel1: TPanel
    Left = 0
    Top = 0
    Width = 137
    Height = 126
    Align = alLeft
    TabOrder = 0
    object ComboBox1: TComboBox
      Left = 1
      Top = 1
      Width = 135
      Height = 21
      Align = alTop
      DropDownCount = 11
      TabOrder = 0
      Text = '38400'
      OnChange = ComboBox1Change
      Items.Strings = (
        '300'
        '600'
        '1200'
        '2400'
        '4800'
        '9600'
        '19200'
        '38400'
        '57600'
        '115200')
    end
    object ListBox1: TListBox
      Left = 1
      Top = 22
      Width = 135
      Height = 103
      Align = alClient
      ItemHeight = 13
      TabOrder = 1
      OnClick = ListBox1Click
    end
  end
  object Panel2: TPanel
    Left = 137
    Top = 0
    Width = 364
    Height = 126
    Align = alClient
    TabOrder = 1
    object Memo1: TMemo
      Left = 1
      Top = 1
      Width = 362
      Height = 124
      Align = alClient
      ReadOnly = True
      TabOrder = 0
    end
  end
  object MainMenu1: TMainMenu
    Left = 152
    Top = 16
    object S9001: TMenuItem
      Caption = '&Menu'
      Hint = 'Click to send samples!'
      ShortCut = 16496
      object MenuGetCatalog: TMenuItem
        Caption = 'Get list of samples and programs'
        Hint = 'Print list of samples in main window'
        ShortCut = 16497
        OnClick = MenuGetCatalogClick
      end
      object N4: TMenuItem
        Caption = '-'
      end
      object MenuGetSample: TMenuItem
        Caption = 'Save sample from machine to .aki file'
        Hint = 
          'Prints sample catalog in right-panel, then click a sample to  im' +
          'port it'
        ShortCut = 16498
        OnClick = MenuGetSampleClick
      end
      object MenuPutSample: TMenuItem
        Caption = 'Send .wav/.aki sample file to machine'
        Hint = 'Prompts for a file-name to sent to the sampler'
        ShortCut = 16499
        OnClick = MenuPutSampleClick
      end
      object N3: TMenuItem
        Caption = '-'
      end
      object MenuGetPrograms: TMenuItem
        Caption = 'Save &programs from machine to .aki file'
        ShortCut = 16500
        OnClick = MenuGetProgramsClick
      end
      object MenuPutPrograms: TMenuItem
        Caption = 'Send .prg programs file to machine'
        ShortCut = 16501
        OnClick = MenuPutProgramsClick
      end
      object N1: TMenuItem
        Caption = '-'
      end
      object MenuUseRightChanForStereoSamples: TMenuItem
        Caption = 'Use right channel for stereo samples'
        Checked = True
        ShortCut = 16502
        OnClick = MenuUseRightChanForStereoSamplesClick
      end
      object MenuAutomaticallyRenameSample: TMenuItem
        Caption = 'Automatically rename sample on machine'
        Checked = True
        ShortCut = 16503
        OnClick = MenuAutomaticallyRenameSampleClick
      end
      object MenuUseHWFlowControlBelow50000Baud: TMenuItem
        Caption = 'Use hardware flow-control below 50,000 baud'
        ShortCut = 16504
        OnClick = MenuUseHWFlowControlBelow50000BaudClick
      end
      object N2: TMenuItem
        Caption = '-'
      end
      object Help1: TMenuItem
        Caption = '&Help'
        ShortCut = 16507
        OnClick = Help1Click
      end
    end
  end
  object Timer1: TTimer
    Interval = 3000
    Left = 152
    Top = 72
  end
  object OpenDialog1: TOpenDialog
    Title = 'Send File To S-Series Sampler'
    Left = 360
    Top = 16
  end
  object SaveDialog1: TSaveDialog
    Left = 296
    Top = 16
  end
  object ApdComPort1: TApdComPort
    Baud = 38400
    TraceName = 'APRO.TRC'
    LogName = 'APRO.LOG'
    Left = 224
    Top = 16
  end
end
