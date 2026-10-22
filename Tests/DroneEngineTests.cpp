#include "Dsp/DroneEngine.h"
#include "Dsp/DroneMath.h"
#include <StillTestReference.h>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace
{
int failures = 0;
void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
struct Stats { double squares=0; float peak=0, maxStep=0; int count=0; };
void measure(const juce::AudioBuffer<float>& b, Stats& s)
{
    for (int c=0;c<b.getNumChannels();++c)
    {
        const auto* p=b.getReadPointer(c);
        for (int i=0;i<b.getNumSamples();++i)
        {
            require(std::isfinite(p[i]), "output remains finite");
            s.peak=std::max(s.peak,std::abs(p[i])); s.squares+=static_cast<double>(p[i])*p[i];
            if(i>0) s.maxStep=std::max(s.maxStep,std::abs(p[i]-p[i-1]));
            ++s.count;
        }
    }
}
double rms(const Stats& s) { return s.count ? std::sqrt(s.squares/s.count) : 0.0; }
}
int main()
{
    const auto closed=still::dsp::openGains(0), half=still::dsp::openGains(0.5f), open=still::dsp::openGains(1);
    require(std::abs(closed.body-.34f)<1e-6f, "closed OPEN retains BODY floor");
    require(std::abs(closed.swirl)<1e-6f, "closed OPEN mutes SWIRL");
    require(half.body>closed.body && half.body<1, "BODY rises through OPEN");
    require(half.swirl>0 && half.swirl<half.body, "SWIRL rises later");
    require(std::abs(open.body-1)<1e-6f && std::abs(open.swirl-1)<1e-6f, "full OPEN reaches unity");
    constexpr int blockSize=512;
    juce::AudioBuffer<float> block(2,blockSize);
    still::dsp::DroneEngine engine;
    require(engine.prepare(48000,1,0,true), "embedded stereo samples decode");
    require(engine.assetsReady(), "runtime audio assets are ready");
    Stats full;
    for(int i=0;i<(48000*10)/blockSize;++i) { engine.process(block,1,0,true); measure(block,full); }
    require(rms(full)>.10, "full-open drone produces a healthy signal");
    require(full.peak<.9f, "full-open render keeps safe headroom");
    require(full.maxStep<.12f, "looped output has no large discontinuity");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto referenceStream=std::make_unique<juce::MemoryInputStream>(
        StillTestReference::D_A_FULL_STAGE_BACKUP_48k_wav,
        static_cast<std::size_t>(StillTestReference::D_A_FULL_STAGE_BACKUP_48k_wavSize),false);
    std::unique_ptr<juce::AudioFormatReader> referenceReader(formats.createReaderFor(std::move(referenceStream)));
    require(referenceReader!=nullptr, "full-open reference decodes for render comparison");
    if(referenceReader!=nullptr)
    {
        constexpr int rampBlocks=8;
        still::dsp::DroneEngine directEngine;
        require(directEngine.prepare(48000,1.0f,0.0f,true), "reference comparison engine prepares");
        for(int i=0;i<rampBlocks;++i) directEngine.process(block,1.0f,0.0f,true);
        juce::AudioBuffer<float> expected(2,blockSize);
        const auto referenceOffset=rampBlocks*blockSize;
        require(referenceReader->read(&expected,0,blockSize,referenceOffset,true,true), "reference segment decodes");
        directEngine.process(block,1.0f,0.0f,true);
        float maxDifference=0.0f;
        for(int c=0;c<2;++c)
            for(int n=0;n<blockSize;++n)
                maxDifference=std::max(maxDifference,std::abs(block.getSample(c,n)-expected.getSample(c,n)));
        require(maxDifference<=5.0e-7f, "48 kHz full-open output matches the supplied reference");
    }

    still::dsp::DroneEngine wetEngine,dryEngine;
    require(wetEngine.prepare(48000,.65f,0,true) && dryEngine.prepare(48000,.65f,0,true), "AIR comparison engines prepare");
    Stats wet,dry;
    juce::AudioBuffer<float> wetBlock(2,blockSize), dryBlock(2,blockSize);
    float maxAirDifference=0.0f;
    for(int i=0;i<240;++i)
    {
        wetEngine.process(wetBlock,.65f,1,true); dryEngine.process(dryBlock,.65f,0,true);
        measure(wetBlock,wet); measure(dryBlock,dry);
        for(int c=0;c<2;++c)
            for(int n=0;n<blockSize;++n)
                maxAirDifference=std::max(maxAirDifference,std::abs(wetBlock.getSample(c,n)-dryBlock.getSample(c,n)));
    }
    require(maxAirDifference>1e-4f, "AIR changes output independently of OPEN");

    juce::AudioBuffer<float> other(2,blockSize);
    still::dsp::DroneEngine rate44;
    require(rate44.prepare(44100,.5f,0,true), "44.1 kHz host prepares");
    Stats alternate;
    for(int i=0;i<44100/blockSize;++i) { rate44.process(other,.5f,0,true); measure(other,alternate); }
    require(alternate.peak<.9f && rms(alternate)>.04, "44.1 kHz render remains active and safe");

    juce::AudioBuffer<float> mono(1,blockSize);
    still::dsp::DroneEngine monoEngine;
    require(monoEngine.prepare(48000,.5f,0,true), "mono engine prepares");
    monoEngine.process(mono,.5f,0,true);
    for(int i=0;i<blockSize;++i) require(std::isfinite(mono.getSample(0,i)), "mono output stays finite");
    if(failures==0) std::puts("STILL DSP smoke checks passed.");
    return failures==0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
