#include "HsneAnalysisPlugin.h"
#include "HsneParameters.h"
#include "HsneScaleAction.h"

#include <PointData.h>

#include <util/Icon.h>

#include <actions/PluginTriggerAction.h>

#include <DimensionsPickerAction.h>

#include <QDebug>
#include <QPainter>

Q_PLUGIN_METADATA(IID "nl.tudelft.HsneAnalysisPlugin")

#ifdef _WIN32
    #include <windows.h>
#endif

using namespace hdps;
using namespace hdps::util;

HsneAnalysisPlugin::HsneAnalysisPlugin(const PluginFactory* factory) :
    AnalysisPlugin(factory),
    _hierarchy(),
    _tsneAnalysis(),
    _hsneSettingsAction(nullptr)
{
    setObjectName("HSNE");
}

HsneAnalysisPlugin::~HsneAnalysisPlugin()
{
}

void HsneAnalysisPlugin::init()
{
    HsneScaleAction::core = _core;

    // Created derived dataset for embedding
    setOutputDataset(_core->createDerivedDataset("HSNE Embedding", getInputDataset(), getInputDataset()));

    getOutputDataset()->getDataHierarchyItem().select();

    // Create new HSNE settings actions
    _hsneSettingsAction = new HsneSettingsAction(this);

    // Collapse the TSNE settings by default
    _hsneSettingsAction->getTsneSettingsAction().getGeneralTsneSettingsAction().collapse();

    // Get input/output datasets
    auto inputDataset  = getInputDataset<Points>();
    auto outputDataset = getOutputDataset<Points>();

    // Set the default number of hierarchy scales based on number of points
    int numHierarchyScales = std::max(1L, std::lround(log10(inputDataset->getNumPoints())) - 2);
    _hsneSettingsAction->getGeneralHsneSettingsAction().getNumScalesAction().setValue(numHierarchyScales);
    _hsneSettingsAction->getGeneralHsneSettingsAction().getNumScalesAction().setDefaultValue(numHierarchyScales);

    std::vector<float> initialData;

    const auto numEmbeddingDimensions = 2;

    initialData.resize(inputDataset->getNumPoints() * numEmbeddingDimensions);

    outputDataset->setData(initialData.data(), inputDataset->getNumPoints(), numEmbeddingDimensions);

    outputDataset->addAction(_hsneSettingsAction->getGeneralHsneSettingsAction());
    outputDataset->addAction(_hsneSettingsAction->getAdvancedHsneSettingsAction());
    outputDataset->addAction(_hsneSettingsAction->getTopLevelScaleAction());
    outputDataset->addAction(_hsneSettingsAction->getTsneSettingsAction().getGeneralTsneSettingsAction());
    outputDataset->addAction(_hsneSettingsAction->getTsneSettingsAction().getAdvancedTsneSettingsAction());
    
    auto dimensionsGroupAction = new GroupAction(this, { &inputDataset->getFullDataset<Points>()->getDimensionsPickerAction() }, true);

    dimensionsGroupAction->setText(QString("Input dimensions (%1)").arg(inputDataset->getFullDataset<Points>()->getGuiName()));
    dimensionsGroupAction->setShowLabels(false);

    outputDataset->addAction(*dimensionsGroupAction);

    outputDataset->getDataHierarchyItem().select();

    auto& computationAction = _hsneSettingsAction->getTsneSettingsAction().getComputationAction();

    const auto updateComputationAction = [this, &computationAction]() {
        const auto isRunning = computationAction.getRunningAction().isChecked();

        computationAction.getStartComputationAction().setEnabled(!isRunning);
        computationAction.getContinueComputationAction().setEnabled(!isRunning && _tsneAnalysis.canContinue());
        computationAction.getStopComputationAction().setEnabled(isRunning);
    };

    connect(&_tsneAnalysis, &TsneAnalysis::progressPercentage, this, [this](const float& percentage) {
        if (getTaskStatus() == DataHierarchyItem::TaskStatus::Aborted)
            return;

        setTaskProgress(percentage);
    });

    connect(&_tsneAnalysis, &TsneAnalysis::progressSection, this, [this](const QString& section) {
        if (getTaskStatus() == DataHierarchyItem::TaskStatus::Aborted)
            return;

        setTaskDescription(section);
    });

    connect(&_tsneAnalysis, &TsneAnalysis::finished, this, [this, &computationAction, updateComputationAction]() {
        setTaskFinished();

        computationAction.getRunningAction().setChecked(false);

        _hsneSettingsAction->getGeneralHsneSettingsAction().getStartAction().setEnabled(false);
        _hsneSettingsAction->setReadOnly(false);

        updateComputationAction();
    });

    connect(&_tsneAnalysis, &TsneAnalysis::aborted, this, [this, &computationAction, updateComputationAction]() {
        setTaskAborted();

        updateComputationAction();

        computationAction.getRunningAction().setChecked(false);

        _hsneSettingsAction->getTsneSettingsAction().getGeneralTsneSettingsAction().setReadOnly(false);
        _hsneSettingsAction->getTsneSettingsAction().getAdvancedTsneSettingsAction().setReadOnly(false);
    });

    connect(&computationAction.getStartComputationAction(), &TriggerAction::triggered, this, [this, &computationAction]() {
        _hsneSettingsAction->getTsneSettingsAction().getGeneralTsneSettingsAction().setReadOnly(true);
        _hsneSettingsAction->getTsneSettingsAction().getAdvancedTsneSettingsAction().setReadOnly(true);

        int topScaleIndex = _hierarchy.getTopScale();
        Hsne::scale_type& topScale = _hierarchy.getScale(topScaleIndex);
        int numLandmarks = topScale.size();
        TsneParameters tsneParameters = _hsneSettingsAction->getTsneSettingsAction().getTsneParameters();

        _tsneAnalysis.startComputation(tsneParameters, _hierarchy.getTransitionMatrixAtScale(topScaleIndex), numLandmarks, _hierarchy.getNumDimensions());
    });

    connect(&computationAction.getContinueComputationAction(), &TriggerAction::triggered, this, [this]() {
        _hsneSettingsAction->getTsneSettingsAction().getGeneralTsneSettingsAction().setReadOnly(true);
        _hsneSettingsAction->getTsneSettingsAction().getAdvancedTsneSettingsAction().setReadOnly(true);

        continueComputation();
    });

    connect(&computationAction.getStopComputationAction(), &TriggerAction::triggered, this, [this]() {
        setTaskDescription("Aborting TSNE");

        qApp->processEvents();

        _tsneAnalysis.stopComputation();
    });
    
    connect(&computationAction.getRunningAction(), &ToggleAction::toggled, this, [this, &computationAction, updateComputationAction](bool toggled) {
        getInputDataset<Points>()->getDimensionsPickerAction().setEnabled(!toggled);
        updateComputationAction();
    });

    connect(&_hsneSettingsAction->getGeneralHsneSettingsAction().getStartAction(), &TriggerAction::triggered, this, [this](bool toggled) {
        _hsneSettingsAction->setReadOnly(true);
        
        setTaskRunning();
        setTaskProgress(0.0f);
        setTaskDescription("Initializing HSNE hierarchy");

        qApp->processEvents();

        std::vector<bool> enabledDimensions = getInputDataset<Points>()->getDimensionsPickerAction().getEnabledDimensions();

        // Initialize the HSNE algorithm with the given parameters
        _hierarchy.initialize(_core, *getInputDataset<Points>(), enabledDimensions, _hsneSettingsAction->getHsneParameters());

        setTaskDescription("Computing top-level embedding");

        qApp->processEvents();

        computeTopLevelEmbedding();
    });

    connect(&_tsneAnalysis, &TsneAnalysis::embeddingUpdate, this, [this](const TsneData& tsneData) {
        auto embedding = getOutputDataset<Points>();

        embedding->setData(tsneData.getData().data(), tsneData.getNumPoints(), 2);

        _hsneSettingsAction->getTsneSettingsAction().getGeneralTsneSettingsAction().getNumberOfComputatedIterationsAction().setValue(_tsneAnalysis.getNumIterations() - 1);

        // NOTE: Commented out because it causes a stack overflow after a couple of iterations
        //QCoreApplication::processEvents();

        _core->notifyDatasetChanged(getOutputDataset());
    });

    updateComputationAction();

    setTaskName("HSNE");
}

void HsneAnalysisPlugin::computeTopLevelEmbedding()
{
    // Get the top scale of the HSNE hierarchy
    int topScaleIndex = _hierarchy.getTopScale();

    Hsne::scale_type& topScale = _hierarchy.getScale(topScaleIndex);
    
    int numLandmarks = topScale.size();

    // Create a subset of the points corresponding to the top level HSNE landmarks,
    // Then create an empty embedding derived from this subset
    auto inputDataset       = getInputDataset<Points>();
    auto selectionDataset   = inputDataset->getSelection<Points>();

    // Select the appropriate points to create a subset from
    selectionDataset->indices.resize(numLandmarks);

    if (inputDataset->isFull())
    {
        for (int i = 0; i < numLandmarks; i++)
            selectionDataset->indices[i] = topScale._landmark_to_original_data_idx[i];
    }
    else
    {
        std::vector<unsigned int> globalIndices;
        inputDataset->getGlobalIndices(globalIndices);
        for (int i = 0; i < numLandmarks; i++)
            selectionDataset->indices[i] = globalIndices[topScale._landmark_to_original_data_idx[i]];
    }

    // Create the subset and clear the selection
    auto subset = inputDataset->createSubsetFromSelection(QString("hsne_scale_%1").arg(topScaleIndex), nullptr, false);

    selectionDataset->indices.clear();

    auto embeddingDataset = getOutputDataset<Points>();
    
    embeddingDataset->setSourceDataSet(subset);
    _hsneSettingsAction->getTopLevelScaleAction().setScale(topScaleIndex);

    _hierarchy.printScaleInfo();

    // Set t-SNE parameters
    HsneParameters hsneParameters = _hsneSettingsAction->getHsneParameters();
    TsneParameters tsneParameters = _hsneSettingsAction->getTsneSettingsAction().getTsneParameters();

    // Add linked selection between the upper embedding and the bottom layer
    {
        LandmarkMap& landmarkMap = _hierarchy.getInfluenceHierarchy().getMap()[topScaleIndex];
        
        hdps::SelectionMap mapping;

        if (inputDataset->isFull())
        {
            for (int i = 0; i < landmarkMap.size(); i++)
            {
                int bottomLevelIdx = _hierarchy.getScale(topScaleIndex)._landmark_to_original_data_idx[i];
                mapping.getMap()[bottomLevelIdx] = landmarkMap[i];
            }
        }
        else
        {
            std::vector<unsigned int> globalIndices;
            inputDataset->getGlobalIndices(globalIndices);
            for (int i = 0; i < landmarkMap.size(); i++)
            {
                std::vector<unsigned int> bottomMap = landmarkMap[i];
                for (int j = 0; j < bottomMap.size(); j++)
                {
                    bottomMap[j] = globalIndices[bottomMap[j]];
                }
                int bottomLevelIdx = _hierarchy.getScale(topScaleIndex)._landmark_to_original_data_idx[i];
                mapping.getMap()[globalIndices[bottomLevelIdx]] = bottomMap;
            }
        }

        embeddingDataset->addLinkedData(inputDataset, mapping);
    }

    // Embed data
    _tsneAnalysis.stopComputation();
    _tsneAnalysis.startComputation(tsneParameters, _hierarchy.getTransitionMatrixAtScale(topScaleIndex), numLandmarks, _hierarchy.getNumDimensions());
}

void HsneAnalysisPlugin::continueComputation()
{
    setTaskRunning();
    setTaskProgress(0.0f);

    _hsneSettingsAction->getTsneSettingsAction().getComputationAction().getRunningAction().setChecked(true);

    _tsneAnalysis.continueComputation(_hsneSettingsAction->getTsneSettingsAction().getTsneParameters().getNumIterations());
}

QIcon HsneAnalysisPluginFactory::getIcon(const QColor& color /*= Qt::black*/) const
{
    return createPluginIcon("HSNE", color);
}

AnalysisPlugin* HsneAnalysisPluginFactory::produce()
{
    return new HsneAnalysisPlugin(this);
}

PluginTriggerActions HsneAnalysisPluginFactory::getPluginTriggerActions(const hdps::Datasets& datasets) const
{
    PluginTriggerActions pluginTriggerActions;

    const auto getPluginInstance = [this](const Dataset<Points>& dataset) -> HsneAnalysisPlugin* {
        return dynamic_cast<HsneAnalysisPlugin*>(Application::core()->requestPlugin(getKind(), { dataset }));
    };

    if (PluginFactory::areAllDatasetsOfTheSameType(datasets, PointType)) {
        if (datasets.count() >= 1) {
            auto pluginTriggerAction = createPluginTriggerAction("HSNE", "Perform HSNE analysis on selected datasets", datasets);

            connect(pluginTriggerAction, &QAction::triggered, [this, getPluginInstance, datasets]() -> void {
                for (auto dataset : datasets)
                    getPluginInstance(dataset);
            });

            pluginTriggerActions << pluginTriggerAction;
        }

        if (datasets.count() >= 2) {
            auto pluginTriggerAction = createPluginTriggerAction("Group/HSNE", "Group datasets and perform HSNE analysis on it", datasets);

            connect(pluginTriggerAction, &QAction::triggered, [this, getPluginInstance, datasets]() -> void {
                getPluginInstance(Application::core()->groupDatasets(datasets));
            });

            pluginTriggerActions << pluginTriggerAction;
        }
    }

    return pluginTriggerActions;
}