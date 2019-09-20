#include "TsneSettingsWidget.h"

// Qt header files:
#include <QDebug>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>

#include <typeinfo>


namespace
{
    template <typename T>
    void AddPushButton(QHBoxLayout& layout, const QString& buttonText, const T slot)
    {
        auto pushButton = std::make_unique<QPushButton>(buttonText);
        QObject::connect(&*pushButton, &QPushButton::clicked, [buttonText, slot]
        {
            try
            {
                slot();
            }
            catch (const std::exception& stdException)
            {
                qCritical()
                    << "Exception \""
                    << typeid(stdException).name()
                    << "\" on "
                    << buttonText
                    << " button click: "
                    << stdException.what();
            }
        });
        layout.addWidget(pushButton.release());
    }

    QString GetSelectionFileFilter()
    {
        return QObject::tr("Text files (*.txt);;All files (*.*)");
    }
}

std::vector<bool> DimensionPickerWidget::getEnabledDimensions() const
{
    std::vector<bool> enabledDimensions(_checkBoxes.size());

    for (unsigned int i = 0; i < _checkBoxes.size(); i++)
        enabledDimensions[i] = _checkBoxes[i]->isChecked();

    return enabledDimensions;
}

void DimensionPickerWidget::setDimensions(unsigned int numDimensions, const std::vector<QString>& names)
{
    _enabledDimensions = std::make_unique<bool[]>(numDimensions);
    std::fill_n(_enabledDimensions.get(), numDimensions, true);

    bool hasNames = names.size() == numDimensions;

    _names = hasNames ? names : std::vector<QString>{};

    clearWidget();

    for (unsigned int i = 0; i < numDimensions; i++)
    {
        QString name = hasNames ? names[i] : QString("Dim ") + QString::number(i);
        auto* const widget = new QCheckBox(name);
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        widget->setMinimumHeight(20);
        widget->setToolTip(name);
        widget->setChecked(true);

        _checkBoxes.push_back(widget);
        const auto row = static_cast<int>(i % (numDimensions / 2));
        const auto column = static_cast<int>(i / (numDimensions / 2));

        _layout.addWidget(widget, row, column);
    }

    resize(160, static_cast<int>(20 * numDimensions / 2));
}


void DimensionPickerWidget::readSelectionFromFile(const QString& fileName)
{
    const std::size_t numberOfDimensions = _names.size();

    std::fill_n(_enabledDimensions.get(), numberOfDimensions, false);

    if (!fileName.isEmpty())
    {
        QFile file(fileName);

        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            while (!file.atEnd())
            {
                const auto timmedLine = file.readLine().trimmed();

                if (!timmedLine.isEmpty())
                {
                    const auto name = QString::fromUtf8(timmedLine);
                    std::size_t i{};

                    [this, &name, &i]
                    {
                        for (const auto& existingName : _names)
                        {
                            if (name == existingName)
                            {
                                _enabledDimensions[i] = true;
                                return;
                            }
                            ++i;
                        }
                        qWarning() << "Failed to select dimension (name not found): " << name;
                    }();
                }
            }
        }
        else
        {
            qCritical() << "Load failed to open file: " << fileName;
        }
    }


    if (_checkBoxes.size() == numberOfDimensions)
    {
        for (std::size_t i{}; i < numberOfDimensions; ++i)
        {
            _checkBoxes[i]->setChecked(_enabledDimensions[i]);
        }
    }
}


void DimensionPickerWidget::writeSelectionToFile(const QString& fileName)
{
    if (!fileName.isEmpty())
    {
        QFile file(fileName);

        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            const std::size_t numberOfDimensions = _names.size();
            Q_ASSERT(_enabledDimensions.get() != nullptr);

            if (_checkBoxes.size() == numberOfDimensions)
            {
                for (std::size_t i{}; i < numberOfDimensions; ++i)
                {
                    const auto* const checkBox = _checkBoxes[i];

                    if (checkBox != nullptr)
                    {
                        _enabledDimensions[i] = checkBox->isChecked();
                    }
                }
            }

            for (std::size_t i{}; i < numberOfDimensions; ++i)
            {
                if (_enabledDimensions[i])
                {
                    file.write(_names[i].toUtf8());
                    file.write("\n");
                }
            }
        }
        else
        {
            qCritical() << "Save failed to open file: " << fileName;
        }
    }
}


void DimensionPickerWidget::clearWidget()
{
    for (QCheckBox* widget : _checkBoxes)
    {
        _layout.removeWidget(widget);
        delete widget;
    }

    _checkBoxes.clear();
}

TsneSettingsWidget::TsneSettingsWidget() {
    setFixedWidth(200);

    connect(&dataOptions,   SIGNAL(currentIndexChanged(QString)), this, SIGNAL(dataSetPicked(QString)));
    connect(&startButton,   &QPushButton::toggled, this, &TsneSettingsWidget::onStartToggled);

    connect(&numIterations, SIGNAL(textChanged(QString)), SLOT(numIterationsChanged(QString)));
    connect(&perplexity,    SIGNAL(textChanged(QString)), SLOT(perplexityChanged(QString)));
    connect(&exaggeration,  SIGNAL(textChanged(QString)), SLOT(exaggerationChanged(QString)));
    connect(&expDecay,      SIGNAL(textChanged(QString)), SLOT(expDecayChanged(QString)));
    connect(&numTrees,      SIGNAL(textChanged(QString)), SLOT(numTreesChanged(QString)));
    connect(&numChecks,     SIGNAL(textChanged(QString)), SLOT(numChecksChanged(QString)));
    connect(&theta,         SIGNAL(textChanged(QString)), SLOT(thetaChanged(QString)));

    // Create group boxes for grouping together various settings
    QGroupBox* settingsBox = new QGroupBox("Basic settings");
    QGroupBox* advancedSettingsBox = new QGroupBox("Advanced Settings");
    QGroupBox* dimensionSelectionBox = new QGroupBox("Dimension Selection");
    
    advancedSettingsBox->setCheckable(true);
    advancedSettingsBox->setChecked(false);
    
    // Build the labels for all the options
    QLabel* iterationLabel = new QLabel("Iteration Count");
    QLabel* perplexityLabel = new QLabel("Perplexity");
    QLabel* exaggerationLabel = new QLabel("Exaggeration");
    QLabel* expDecayLabel = new QLabel("Exponential Decay");
    QLabel* numTreesLabel = new QLabel("Number of Trees");
    QLabel* numChecksLabel = new QLabel("Number of Checks");

    // Set option default values
    numIterations.setFixedWidth(50);
    perplexity.setFixedWidth(50);
    exaggeration.setFixedWidth(50);
    expDecay.setFixedWidth(50);
    numTrees.setFixedWidth(50);
    numChecks.setFixedWidth(50);

    numIterations.setValidator(new QIntValidator(1, 10000, this));
    perplexity.setValidator(new QIntValidator(2, 50, this));
    exaggeration.setValidator(new QIntValidator(1, 10000, this));
    expDecay.setValidator(new QIntValidator(1, 10000, this));
    numTrees.setValidator(new QIntValidator(1, 10000, this));
    numChecks.setValidator(new QIntValidator(1, 10000, this));

    numIterations.setText("1000");
    perplexity.setText("30");
    exaggeration.setText("250");
    expDecay.setText("70");
    numTrees.setText("4");
    numChecks.setText("1024");

    startButton.setText("Start Computation");
    startButton.setFixedSize(QSize(150, 50));
    startButton.setCheckable(true);

    // Add options to their appropriate group box
    auto* const settingsLayout = new QVBoxLayout();
    settingsLayout->addWidget(iterationLabel);
    settingsLayout->addWidget(&numIterations);
    settingsLayout->addWidget(perplexityLabel);
    settingsLayout->addWidget(&perplexity);
    settingsBox->setLayout(settingsLayout);

    auto* const dimensionSelectionLayout = new QVBoxLayout();
    auto* const scroller = new QScrollArea();
    scroller->setMinimumHeight(50);
    scroller->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    scroller->setWidget(&_dimensionPickerWidget);
    dimensionSelectionLayout->addWidget(scroller);
    dimensionSelectionBox->setLayout(dimensionSelectionLayout);

    dimensionSelectionLayout->addLayout([this]
    {
        auto hboxLayout = std::make_unique<QHBoxLayout>();

        AddPushButton(*hboxLayout, "Load", [this]
        {
            const auto fileName = QFileDialog::getOpenFileName(this,
                QObject::tr("Dimension selection"), {}, GetSelectionFileFilter());
            _dimensionPickerWidget.readSelectionFromFile(fileName);
        });
        AddPushButton(*hboxLayout, "Save", [this]
        {
            const auto fileName = QFileDialog::getSaveFileName(this,
                QObject::tr("Dimension selection"), {}, GetSelectionFileFilter());
            _dimensionPickerWidget.writeSelectionToFile(fileName);
        });
        return hboxLayout;
    }().release());

    auto* const advancedSettingsLayout = new QGridLayout();
    advancedSettingsLayout->addWidget(exaggerationLabel, 0, 0);
    advancedSettingsLayout->addWidget(&exaggeration, 1, 0);
    advancedSettingsLayout->addWidget(expDecayLabel, 0, 1);
    advancedSettingsLayout->addWidget(&expDecay, 1, 1);
    advancedSettingsLayout->addWidget(numTreesLabel, 2, 0);
    advancedSettingsLayout->addWidget(&numTrees, 3, 0);
    advancedSettingsLayout->addWidget(numChecksLabel, 2, 1);
    advancedSettingsLayout->addWidget(&numChecks, 3, 1);
    advancedSettingsBox->setLayout(advancedSettingsLayout);

    // Add all the parts of the settings widget together
    addWidget(&dataOptions);
    addWidget(settingsBox);
    addWidget(dimensionSelectionBox);
    addWidget(advancedSettingsBox);
    addWidget(&startButton);
}



// Communication with the dimension picker widget
void TsneSettingsWidget::onNumDimensionsChanged(TsneAnalysisPlugin*, unsigned int numDimensions, const std::vector<QString>& names)
{
    _dimensionPickerWidget.setDimensions(numDimensions, names);
}

std::vector<bool> TsneSettingsWidget::getEnabledDimensions()
{
    return _dimensionPickerWidget.getEnabledDimensions();
}


QString TsneSettingsWidget::currentData()
{
    return dataOptions.currentText();
}

// Check if all input values are valid
bool TsneSettingsWidget::hasValidSettings()
{
    if (!numIterations.hasAcceptableInput())
        return false;
    if (!perplexity.hasAcceptableInput())
        return false;
    if (!exaggeration.hasAcceptableInput())
        return false;
    if (!expDecay.hasAcceptableInput())
        return false;
    if (!numTrees.hasAcceptableInput())
        return false;
    if (!numChecks.hasAcceptableInput())
        return false;

    return true;
}

void TsneSettingsWidget::checkInputStyle(QLineEdit& input)
{
    if (input.hasAcceptableInput())
    {
        input.setStyleSheet("");
    }
    else
    {
        input.setStyleSheet("border: 1px solid red");
    }
}


// SLOTS
void TsneSettingsWidget::onStartToggled(bool pressed)
{
    // Do nothing if we have no data set selected
    if (dataOptions.currentText().isEmpty()) {
        return;
    }

    // Check if the tSNE settings are valid before running the computation
    if (!hasValidSettings()) {
        QMessageBox warningBox;
        warningBox.setText(tr("Some settings are invalid or missing. Continue with default values?"));
        QPushButton *continueButton = warningBox.addButton(tr("Continue"), QMessageBox::ActionRole);
        QPushButton *abortButton = warningBox.addButton(QMessageBox::Abort);

        warningBox.exec();

        if (warningBox.clickedButton() == abortButton) {
            return;
        }
    }
    startButton.setText(pressed ? "Stop Computation" : "Start Computation");
    emit pressed ? startComputation() : stopComputation();
}

void TsneSettingsWidget::numIterationsChanged(const QString &)
{
    checkInputStyle(numIterations);
}

void TsneSettingsWidget::perplexityChanged(const QString &)
{
    checkInputStyle(perplexity);
}

void TsneSettingsWidget::exaggerationChanged(const QString &)
{
    checkInputStyle(exaggeration);
}

void TsneSettingsWidget::expDecayChanged(const QString &)
{
    checkInputStyle(expDecay);
}

void TsneSettingsWidget::numTreesChanged(const QString &)
{
    checkInputStyle(numTrees);
}

void TsneSettingsWidget::numChecksChanged(const QString &)
{
    checkInputStyle(numChecks);
}

void TsneSettingsWidget::thetaChanged(const QString& )
{
    checkInputStyle(theta);
}