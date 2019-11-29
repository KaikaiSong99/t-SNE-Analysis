#include "TsneAnalysisPlugin.h"

#include "PointData.h"

#include <QtCore>
#include <QtDebug>

Q_PLUGIN_METADATA(IID "nl.tudelft.TsneAnalysisPlugin")
#include <windows.h>

#include <set>

// =============================================================================
// View
// =============================================================================

using namespace hdps;

TsneAnalysisPlugin::~TsneAnalysisPlugin(void)
{
    stopComputation();
}

void TsneAnalysisPlugin::init()
{
    _settings = std::make_unique<TsneSettingsWidget>();

    connect(_settings.get(), &TsneSettingsWidget::dataSetPicked, this, &TsneAnalysisPlugin::dataSetPicked);
    connect(_settings.get(), &TsneSettingsWidget::startComputation, this, &TsneAnalysisPlugin::startComputation);
    connect(_settings.get(), &TsneSettingsWidget::stopComputation, this, &TsneAnalysisPlugin::stopComputation);
    connect(&_tsne, &TsneAnalysis::computationStopped, _settings.get(), &TsneSettingsWidget::computationStopped);
    connect(&_tsne, SIGNAL(newEmbedding()), this, SLOT(onNewEmbedding()));
}

void TsneAnalysisPlugin::dataAdded(const QString name)
{
    _settings->dataOptions.addItem(name);
}

void TsneAnalysisPlugin::dataChanged(const QString name)
{
    if (name != _settings->currentData()) {
        return;
    }

    IndexSet& set = (IndexSet&)_core->requestSet(name);

    PointData& rawData = set.getData<PointData>();
    _settings->dataChanged(rawData);
}

void TsneAnalysisPlugin::dataRemoved(const QString name)
{
    
}

void TsneAnalysisPlugin::selectionChanged(const QString dataName)
{

}


QStringList TsneAnalysisPlugin::supportedDataKinds()
{
    QStringList supportedKinds;
    supportedKinds << "Points";
    return supportedKinds;
}

SettingsWidget* const TsneAnalysisPlugin::getSettings()
{
    return _settings.get();
}

void TsneAnalysisPlugin::dataSetPicked(const QString& name)
{
    IndexSet& set = (IndexSet&)_core->requestSet(name);
    PointData& rawData = set.getData<PointData>();

    _settings->dataChanged(rawData);
}

void TsneAnalysisPlugin::startComputation()
{
    initializeTsne();

    // Run the computation
    QString setName = _settings->dataOptions.currentText();
    const IndexSet& set = dynamic_cast<const IndexSet&>(_core->requestSet(setName));
    const PointData& points = set.getData<PointData>();
	
	std::vector<bool> enabledDimensions = _settings->getEnabledDimensions();

	unsigned int numDimensions = count_if(enabledDimensions.begin(), enabledDimensions.end(), [](bool b) { return b; });

    // Create list of data from the enabled dimensions
    std::vector<float> data;
    
	data.reserve(points.getNumPoints() * numDimensions);
	
	const auto selectionSet = std::set<std::uint32_t>(set.indices.begin(), set.indices.end());

    for (int i = 0; i < points.getNumPoints(); i++)
    {
        for (int j = 0; j < points.getNumDimensions(); j++)
        {
			if (enabledDimensions[j]) {
				const auto index	= i * points.getNumDimensions() + j;
				const auto selected = selectionSet.find(index) != selectionSet.end();

				if (set.isFull() || selected) {
					data.push_back(points[index]);
				}
			}
        }
    }
    

    _embedSetName = _core->createDerivedData("Points", "Embedding", points.getName());
    const IndexSet& embedSet = dynamic_cast<const IndexSet&>(_core->requestSet(_embedSetName));
    PointData& embedPoints = embedSet.getData<PointData>();

    embedPoints.setData(nullptr, 0, 2);
    _core->notifyDataAdded(_embedSetName);

    // Compute t-SNE with the given data
    _tsne.initTSNE(data, numDimensions);

    _tsne.start();
}

void TsneAnalysisPlugin::onNewEmbedding() {
    const TsneData& outputData = _tsne.output();
    const IndexSet& embedSet = dynamic_cast<const IndexSet&>(_core->requestSet(_embedSetName));
    PointData& embedPoints = embedSet.getData<PointData>();

    embedPoints.setData(outputData.getData().data(), outputData.getNumPoints(), 2);

    _core->notifyDataChanged(_embedSetName);
}

void TsneAnalysisPlugin::initializeTsne() {
    // Initialize the tSNE computation with the settings from the settings widget
    _tsne.setIterations(_settings->numIterations.text().toInt());
    _tsne.setPerplexity(_settings->perplexity.text().toInt());
    _tsne.setExaggerationIter(_settings->exaggeration.text().toInt());
    _tsne.setNumTrees(_settings->numTrees.text().toInt());
    _tsne.setNumChecks(_settings->numChecks.text().toInt());
}

void TsneAnalysisPlugin::stopComputation() {
    if (_tsne.isRunning())
    {
        // Request interruption of the computation
        _tsne.stopGradientDescent();
        _tsne.exit();

        // Wait until the thread has terminated (max. 3 seconds)
        if (!_tsne.wait(3000))
        {
            qDebug() << "tSNE computation thread did not close in time, terminating...";
            _tsne.terminate();
            _tsne.wait();
        }
        qDebug() << "tSNE computation stopped.";
    }
}

// =============================================================================
// Factory
// =============================================================================

AnalysisPlugin* TsneAnalysisPluginFactory::produce()
{
    return new TsneAnalysisPlugin();
}
