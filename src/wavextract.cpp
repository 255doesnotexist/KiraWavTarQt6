#include "wavextract.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <kfr/all.hpp>
#include <algorithm>
#include <QDir>
#include "wavtar_utils.h"
#include <kira/lib_helper/kfr_helper.h>
#include <kira/base64.h>
#include <QCoreApplication>
#include <QtConcurrent>
#include <exception>

using namespace wavtar_defines;
using namespace wavtar_utils;

namespace WAVExtract {

    CheckResult preCheck(QString srcWAVFileName, QString dstDirName)
    {
        auto descFileName = getDescFileNameFrom(srcWAVFileName);

        if (!(QFileInfo{srcWAVFileName}.exists() && QFileInfo{descFileName}.exists()))
            return {CheckPassType::CRITICAL, QCoreApplication::translate("WAVExtract", "<p class='critical'>The file given or its description file don't exists.</p>"), {}};

        QFile descFileDevice{descFileName};

        if (!descFileDevice.open(QFile::Text | QFile::ReadOnly))
            return {CheckPassType::CRITICAL, QCoreApplication::translate("WAVExtract", "<p class='critical'>The description can not be opened.</p>"), {}};

        auto descFileByteData = descFileDevice.readAll();
        auto descJsonDoc = QJsonDocument::fromJson(descFileByteData);
        auto descRoot = descJsonDoc.object();

        if (descRoot.value("version").toInt() != desc_file_version)
             return {CheckPassType::CRITICAL, QCoreApplication::translate("WAVExtract", "<p class='critical'>The desctiption file has a incompatible version.</p>"), {}};

        bool openSuccess = false;
        kfr::audio_reader_wav<sample_process_t> reader(kfr::open_qt_file_for_reading(srcWAVFileName, &openSuccess));
        if (!openSuccess)
            return {CheckPassType::CRITICAL, QCoreApplication::translate("WAVExtract", "<p class='critical'>The wav file can not be opened.</p>"), {}};

        QString warningMsg;
        auto totalLength = decodeBase64<qint64>(descRoot.value("total_length").toString());
        if (reader.format().length != totalLength)
            warningMsg.append(QCoreApplication::translate("WAVExtract", "<p class='warning'>The wav file has a different length (%1 samples) than expected (%2 samples). "
                                                                        "Sample rate changed or time has been changed?</p>").arg(reader.format().length).arg(totalLength));
        auto expectedSampleRate = descRoot.value("sample_rate").toDouble();
        if (reader.format().samplerate != expectedSampleRate)
            warningMsg.append(QCoreApplication::translate("WAVExtract", "<p class='warning'>Sample rate (%1) of the given file is not same as it supposed to be (%2) in description file. "
                                                                        "We will resample it.</p>")
                              .arg(reader.format().samplerate).arg(expectedSampleRate));
        auto expectedChannelCount = descRoot.value("channel_count").toInt();
        if ((qint64) reader.format().channels != expectedChannelCount)
            warningMsg.append(QCoreApplication::translate("WAVExtract", "<p class='warning'>Channel count (%1) of the given file is not same as it supposed to be (%2) in description file.</p>")
                              .arg(reader.format().channels).arg(expectedChannelCount));

        if (!QDir{dstDirName}.isEmpty())
            warningMsg.append(QCoreApplication::translate("WAVExtract", "<p class='warning'>It seems like that target folder is not empty. "
                                                                        "All files have name conflicts will be replaced, so BACKUP if needed to.</p>"));

        return {warningMsg.isEmpty() ? CheckPassType::OK : CheckPassType::WARNING, warningMsg, descRoot};
    }

    SrcData readSrcWAVFile(QString srcWAVFileName, QJsonObject descRoot){
        bool openSuccess = false;
        kfr::audio_reader_wav<sample_process_t> reader{kfr::open_qt_file_for_reading(srcWAVFileName, &openSuccess)};
        if (!openSuccess){
            throw wavtar_exceptions::runtime_error(QCoreApplication::translate("WAVExtract", "Error occurred when opening \"%1\".").arg(srcWAVFileName));
        }
        auto data = std::make_shared<kfr::univector2d<sample_process_t>>(reader.read_channels());
        if (data->empty()){
            throw wavtar_exceptions::runtime_error(QCoreApplication::translate("WAVExtract", "There have no data in file \"%1\".").arg(srcWAVFileName));
        }

        auto expectedChannelCount = descRoot.value("channel_count").toInt();
        if ((qint64) reader.format().channels < expectedChannelCount)
        {
            for (auto i = 0; i < expectedChannelCount - (qint64) reader.format().channels; ++i){
                       data->emplace_back(reader.format().length);
                   }
        }
        else if ((qint64) reader.format().channels > expectedChannelCount){
            // Manual erase to avoid std::move assignment ambiguities with KFR univectors
            size_t new_size = expectedChannelCount;
            while (data->size() > new_size) {
                data->pop_back();
            }
        }


        auto expectedSampleRate = descRoot.value("sample_rate").toDouble();
        if (!qFuzzyCompare(reader.format().samplerate, expectedSampleRate))
        {
            for (auto it = data->begin(); it != data->end(); ++it){
                auto& srcData = *it;
                // Simple resampling workaround to avoid KFR samplerate_converter assignment issues
                size_t inputLength = srcData.size();
                size_t outputLength = (inputLength * expectedSampleRate) / reader.format().samplerate;

                if (outputLength > 0) {
                    kfr::univector<sample_process_t> resampled(outputLength);

                    // Simple linear interpolation resampling
                    double ratio = static_cast<double>(reader.format().samplerate) / static_cast<double>(expectedSampleRate);

                    for (size_t i = 0; i < outputLength; ++i) {
                        double inputIndex = i * ratio;
                        size_t index0 = static_cast<size_t>(inputIndex);
                        size_t index1 = std::min(index0 + 1, inputLength - 1);
                        double fraction = inputIndex - index0;

                        // Linear interpolation
                        resampled[i] = srcData[index0] * (1.0 - fraction) + srcData[index1] * fraction;
                    }

                    // Use swap to avoid assignment ambiguity
                    srcData.swap(resampled);
                }
            }
        }

        auto expectedLength = decodeBase64<qint64>(descRoot.value("total_length").toString());
        if (reader.format().length != expectedLength){
            for (auto it = data->begin(); it != data->end(); ++it){
                kfr::univector<sample_process_t> trimmed(expectedLength);
                auto& src = *it;
                auto srcEnd = ((qint64) src.size() < expectedLength) ? src.end() : src.begin() + expectedLength;
                std::copy(src.begin(), srcEnd, trimmed.begin());
                // Replace src contents with trimmed data to avoid assignment ambiguity
                src.resize(trimmed.size());
                std::copy(trimmed.begin(), trimmed.end(), src.begin());
            }
        }

        return {data, expectedSampleRate, descRoot.value("descriptions").toArray()};
    }

    //Use format in description file, when targetFormat's type == unknown
    QFuture<QList<ExtractErrorDescription>> startExtract(std::shared_ptr<kfr::univector2d<sample_process_t>> srcData, decltype(kfr::audio_format::samplerate) srcSampleRate, QJsonArray descArray, QString dstDirName, kfr::audio_format targetFormat, bool removeDCOffset){
        //We change the array to VariantList here, as Qt 5.15.2 seems not copy QJsonArray correctly here. It may be a bug with the implicit sharing with the QJsonArray. Qt 6 fixes it though.
        //NOTE: if moved to Qt 6, change the behaviour here.

        auto filteredDescVariantList = QtConcurrent::filtered(descArray.toVariantList(), std::function([](const QVariant& value)->bool{
            return value.toJsonObject().value("selected").toBool(true);
        })).results();

        auto descObjList = QtConcurrent::mapped(filteredDescVariantList,std::function([](const QVariant& value)->QJsonObject{
            return value.toJsonObject();
        })).results();

        if (removeDCOffset)
        {
            for (auto it = srcData->begin(); it != srcData->end(); ++it){
                // Evaluate the dcremove expression and store result
                auto expr_result = kfr::dcremove(*it);
                // Create a temporary univector to hold the result by direct assignment
                kfr::univector<sample_process_t> temp_result = expr_result;
                // Use swap to avoid assignment ambiguity
                it->swap(temp_result);
            }
        }

        return QtConcurrent::mappedReduced<QList<ExtractErrorDescription>>(descObjList, std::function([srcSampleRate, targetFormat, srcData, dstDirName](const QJsonObject& descObj) mutable -> ExtractErrorDescription{
            //Use format from desc file if user ask to
            if (targetFormat.type == kfr::audio_sample_type::unknown){
                targetFormat.samplerate = descObj.value("sample_rate").toDouble();
                targetFormat.type = (kfr::audio_sample_type) descObj.value("sample_type").toInt();
                targetFormat.wav_format = (kfr::audio_format::wav_format_t) descObj.value("wav_format").toInt();
                targetFormat.channels = descObj.value("channel_count").toInt();
            }

            auto beginIndex = decodeBase64<qint64>(descObj.value("begin_index").toString());
            auto length = decodeBase64<qint64>(descObj.value("length").toString());
            auto segmentData = kfr::univector2d<sample_process_t>(targetFormat.channels);
            for (decltype (targetFormat.channels) i = 0; i < targetFormat.channels; ++i){
                auto& segmentChannel = segmentData[i];
                auto tempData = kfr::univector<sample_process_t>(srcData->at(i).begin() + beginIndex, srcData->at(i).begin() + beginIndex + length);
                segmentChannel.resize(tempData.size());
                std::copy(tempData.begin(), tempData.end(), segmentChannel.begin());
                if (!qFuzzyCompare(srcSampleRate, targetFormat.samplerate)){
                    // Simple resampling workaround to avoid KFR samplerate_converter assignment issues
                    size_t inputLength = segmentChannel.size();
                    size_t outputLength = (inputLength * targetFormat.samplerate) / srcSampleRate;

                    if (outputLength > 0) {
                        kfr::univector<sample_process_t> resampled(outputLength);

                        // Simple linear interpolation resampling
                        double ratio = static_cast<double>(srcSampleRate) / static_cast<double>(targetFormat.samplerate);

                        for (size_t i = 0; i < outputLength; ++i) {
                            double inputIndex = i * ratio;
                            size_t index0 = static_cast<size_t>(inputIndex);
                            size_t index1 = std::min(index0 + 1, inputLength - 1);
                            double fraction = inputIndex - index0;

                            // Linear interpolation
                            resampled[i] = segmentChannel[index0] * (1.0 - fraction) + segmentChannel[index1] * fraction;
                        }

                        // Use swap to avoid assignment ambiguity
                        segmentChannel.swap(resampled);
                    }
                }
            }

            auto fileName = descObj.value("file_name").toString();
            auto absoluteFileName = QDir(dstDirName).absoluteFilePath(fileName);
            //makes folder if current subdir not exist
            QFileInfo {absoluteFileName}.absoluteDir().mkpath(".");
            bool openSuccess;
            auto writer = kfr::audio_writer_wav<sample_process_t>(kfr::open_qt_file_for_writing(absoluteFileName, &openSuccess), targetFormat);
            if (!openSuccess){
                return {QCoreApplication::translate("WAVExtract", "Error occurred when writing into \"%1\".").arg(absoluteFileName), descObj, srcData, srcSampleRate};
            }

            size_t to_write = 0;
            auto written = kfr::write_mutlichannel_wav_file<sample_process_t>(writer, segmentData, &to_write);
            if (to_write == written)
                return {};
            else
                return {QCoreApplication::translate("WAVExtract", "File \"%1\" can not being fully written. "
                                                                  "%2 bytes has been written into file \"%1\", "
                                                                  "which is expected to be %3.")
                            .arg(absoluteFileName).arg(written).arg(to_write),
                            descObj, srcData, srcSampleRate};
        }),
        std::function([](QList<ExtractErrorDescription>& result, const ExtractErrorDescription& stepResult){
            if (!stepResult.description.isEmpty())
                result.append(stepResult);
        }));
    }
}
