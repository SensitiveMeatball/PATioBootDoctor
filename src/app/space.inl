// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- free up space
    void startMeasure(const Fix& f) {
        if (f.install < 0 || f.install >= (int)rep_.installs.size()) return;
        const Install& in = rep_.installs[f.install];
        wstr root = rep_.m.parts[in.part].root + in.folder;
        auto spots = std::make_shared<std::vector<CleanSpot>>(cleanupSpots());
        runJob(T(L"Measuring what can be removed"), in.label(), true,
               [this, root, spots] { measureSpots(root, *spots, &cancel_, [this](const wstr& s, float fr) { progress(s, fr); }); },
               [this, f, spots] {
                   if (!cancel_) cleanForm(f, *spots);
               });
    }

    void cleanForm(const Fix& f, const std::vector<CleanSpot>& spots) {
        using F = kit::FormField;
        std::vector<F> fields;
        uint64_t total = 0;
        for (size_t k = 0; k < spots.size(); ++k) {
            F t;
            t.kind = F::Toggle;
            t.id = (int)k + 1;
            t.label = spots[k].title + L" · " + sizeText(spots[k].bytes);
            t.tip = spots[k].tip;
            t.value = spots[k].bytes > 0 ? 1 : 0;
            if (t.value) total += spots[k].bytes;
            fields.push_back(t);
        }
        F n;
        n.kind = F::Note;
        n.label = TF(L"About {0} can be freed. Personal files, programs and settings are not touched. Files in use are left alone.", sizeText(total));
        fields.push_back(n);
        showForm(T(L"Free up space"), fields, T(L"Remove"), [this, f, spots](const std::vector<F>& r) {
            Fix x = f;
            x.arg.assign(spots.size(), L'0');
            for (const F& v : r)
                if (v.kind == F::Toggle && v.id >= 1 && v.id <= (int)spots.size() && v.value) x.arg[v.id - 1] = L'1';
            if (x.arg.find(L'1') == wstr::npos) return;
            startFixes({x});
        });
    }
