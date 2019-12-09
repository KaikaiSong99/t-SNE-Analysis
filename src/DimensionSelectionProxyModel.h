#pragma once
#include <QSortFilterProxyModel>

namespace hdps
{
    class DimensionSelectionHolder;

    class DimensionSelectionProxyModel final : public QSortFilterProxyModel
    {
        const DimensionSelectionHolder& _holder;
        double _minimumStandardDeviation;
        bool _filterShouldAcceptOnlySelected = false;
        bool _filterShouldApplyExclusion = false;
        std::vector<QString> _exclusion;

    public:
        explicit DimensionSelectionProxyModel(const DimensionSelectionHolder& holder);

        void SetMinimumStandardDeviation(const double minimumStandardDeviation);

        void SetFilterShouldAcceptOnlySelected(bool);

        void SetFilterShouldAppyExclusion(const bool arg)
        {
            _filterShouldApplyExclusion = arg;
        }

        void SetExclusion(std::vector<QString> arg)
        {
            _exclusion = std::move(arg);
        }

    private:
        bool lessThan(const QModelIndex &modelIndex1, const QModelIndex &modelIndex2) const override;

    public:
        bool filterAcceptsRow(const int sourceRow, const QModelIndex &sourceParent) const override;
    };

}

