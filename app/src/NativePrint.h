// 악보 인쇄: PDF 파일을 운영체제의 인쇄 창으로 보낸다 (macOS 는 PDFKit). 실패하면 error
#pragma once

#include <juce_core/juce_core.h>

namespace daw
{

#if JUCE_MAC
bool printPdfFile (const juce::File& pdf, juce::String& error);
#else
inline bool printPdfFile (const juce::File&, juce::String& error)
{
    error = juce::String::fromUTF8 ("이 운영체제에서는 악보 인쇄를 아직 지원하지 않습니다. 악보 PDF 내보내기로 쓴 파일을 인쇄합니다.");
    return false;
}
#endif

} // namespace daw
