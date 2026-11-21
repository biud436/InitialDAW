#include "NativePrint.h"

#import <AppKit/AppKit.h>
#import <PDFKit/PDFKit.h>

namespace daw
{

bool printPdfFile (const juce::File& pdf, juce::String& error)
{
    @autoreleasepool
    {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:pdf.getFullPathName().toRawUTF8()]];
        PDFDocument* document = [[[PDFDocument alloc] initWithURL:url] autorelease];
        if (document == nil || [document pageCount] == 0)
        {
            error = juce::String::fromUTF8 ("인쇄할 악보 PDF 를 열 수 없습니다: ") + pdf.getFullPathName();
            return false;
        }
        NSPrintInfo* info = [[[NSPrintInfo sharedPrintInfo] copy] autorelease];
        [info setTopMargin:0.0];
        [info setBottomMargin:0.0];
        [info setLeftMargin:0.0];
        [info setRightMargin:0.0];
        NSPrintOperation* operation = [document printOperationForPrintInfo:info scalingMode:kPDFPrintPageScaleToFit autoRotate:YES];
        if (operation == nil)
        {
            error = juce::String::fromUTF8 ("인쇄를 준비하지 못했습니다.");
            return false;
        }
        [operation setShowsPrintPanel:YES];
        [operation setShowsProgressPanel:YES];
        [operation setJobTitle:[NSString stringWithUTF8String:pdf.getFileNameWithoutExtension().toRawUTF8()]];
        [operation runOperation];
        return true;
    }
}

} // namespace daw
